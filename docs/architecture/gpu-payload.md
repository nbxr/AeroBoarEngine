# GPU payload — what we upload, why, and where it can shrink

**Purpose:** Inventory every CPU→GPU (and GPU-resident) payload the engine keeps, with stride / cadence / rationale. Use this to decide **compaction** work. Not a frame-graph doc.

**Hardware constraint:** Adreno 740 is UMA. `HostWrite` mapped buffers *are* the GPU copy — extra staging doubles traffic (`BufferUtils.h`). Compaction of **per-frame CPU writes** is CPU store bandwidth + L2. Compaction of **static** buffers is VRAM footprint and vertex fetch. Compaction of **attachments** (shadow, HZB) is the large DRAM/GMEM cost.

**FIF:** `MAX_FRAMES_IN_FLIGHT = 2`. Anything the CPU writes and the GPU reads the same frame is doubled unless noted.

**Layouts:** Graphics UBOs are **std140** (vec4/mat4 only — no scalar arrays). SSBOs and compute are **std430**. Push constants are 80 B or 32 B.

Example working set (ABeautifulGame-scale, post-meshopt): ~22 primitives, ~56 render meshes, ~3.8×10⁵ verts, ~2.5×10⁶ uint32 indices. Chess/SMB numbers differ; formulas stay the same.

---

## 1. Cadence map (what actually moves)

| Cadence | Payloads |
|---------|----------|
| **Once at load** (then GPU-only or toggle-once) | Vertex + index + `MeshPrimitiveSSBO`, materials, bindless textures, IBL cube+LUT, meshlet table, IBM + joint meta, dummy images |
| **Every frame, CPU write** | `worlds[]` (all transforms), `FrameConstants`, lights if dirty, `GpuCullGlobals` (once per cull dispatch), morph vertex patches, HUD/debug verts |
| **Every frame, GPU write** (not a CPU upload) | Joint palette, `out_instances[]`, indirect cmds, meshlet cmds, shadow array, HZB pyramid |
| **Never / unused dual view** | Vertex buffer is also bound as SSBO (binding 4) — same allocation, extra descriptor |

**Per-frame CPU write ranking (typical chess/SMB, occlusion off):**

1. `worlds[]` — `n_transforms × 64 B` (often hundreds of nodes: tens of KB)
2. Morph — **full `Vertex` (64 B) × morph verts**, both FIF sides (`write_primitive_vertices`)
3. `GpuCullGlobals` — 464 B × (3 shadow cascades + 1–3 camera culls)
4. `FrameConstants` — 432 B
5. HUD / F3 debug lines — small unless Jolt debug is dense
6. Lights — 8 × 64 B, only when `lights_upload_mask_` is set

Vertex/index/material/texture/shadow **memory** dwarfs that. Frame-time compaction should target **(1)(2)** first; memory compaction should target **vertex/index + shadow**.

---

## 2. Geometry (static + morph)

### 2.1 `gfx::Vertex` — 64 B stride

| Offset | Field | Format | Why |
|--------|--------|--------|-----|
| 0 | `position[3]` | 3×f32 | World-space positions; morph writes these |
| 12 | `normal[3]` | 3×f32 | PBR; morph may rewrite |
| 24 | `tangent[4]` | 4×f32 | xyz + handedness; needed for normal maps |
| 40 | `uv[8]` | 4×f16 | UV0+UV1 packed (`R16G16B16A16_SFLOAT`) |
| 48 | `color[4]` | RGBA8 | glTF `COLOR_0`; white if missing |
| 52 | `blend_weights[4]` | UNORM8 | Skin |
| 56 | `blend_indices[4]` | UINT8 | Skin |
| 60 | `_pad[4]` | — | Keep 64 B; meshopt weld uses full-struct equality |

**Why 64:** cache-line friendly, VS input alignment, one cache line per vert on many mobile paths. Comment in `Vertex.h`: RGBA8 added ~14% vs an old 56 B layout.

**Resident size:** `n_verts × 64` (double-buffered while `DoubleBufferedBuffer` is used). Example 376k verts ≈ **24 MB** (×2 if both upload/render sides stay allocated).

**Also bound as SSBO** (binding 4). Draw path uses **vertex attributes**. SSBO view is leftover for a compute/debug path — same bytes, extra bind.

**Indices:** `uint32` always (`gfx::Index`). Example 2.5M × 4 ≈ **10 MB**. Many Kenney/chess meshes would fit `uint16`.

**MeshPrimitiveSSBO** (binding 3): 16 B `{vertex_offset, vertex_count, index_offset, index_count}` per primitive. Tiny. Needed if anything fetches by primitive id; draws use `vertex_offset` / `first_index` on the indirect command instead.

### 2.2 Morph

CPU blends targets, then `MeshManager::write_primitive_vertices` **memcpy of full 64 B verts onto both FIF vertex buffers**. A morph cube with 1k verts is 64 KB/frame; a character morph would be worse.

Only position/normal typically change. Pad, UVs, color, skin are rewritten anyway.

---

## 3. Per-instance / transform (hot)

### 3.1 `worlds[]` — GpuCulling SSBO

- **Stride:** `mat4` = 64 B  
- **Count:** `TransformManager` node count (every glTF node, not just drawables)  
- **Cadence:** memcpy **all** worlds after the FIF fence (`sync_worlds`)  
- **Consumers:** frustum cull (transform local AABB), `skin_palette.comp` (`inv(meshWorld) * jointWorld * IBM`)

**Why full mat4:** non-uniform scale and shear from glTF; inverse of mesh world for skin. Affine 3×4 (48 B) is enough if we never need a full inverse in the VS (compute can still invert 3×3+translation).

**Duplication:** visible instances also store `DrawInstanceGPU.model` (another 64 B). The VS does **not** read `worlds[]`.

### 3.2 `DrawInstanceGPU` — binding 1, GPU-written

```
mat4  model;   // 64
uvec4 meta;    // 16  x=material, y=joint_base, z=joint_count, w=unused
```

**80 B**, std430. Packed by `cull_frustum.comp` into a **fixed per-batch region** (opaque half, then optional transparent half at `instance_base_offset`). Graphics binding 1 never flips mid-CB (`UPDATE_AFTER_BIND`).

**Capacity:** `sum(instances per mesh batch)` × 1 or 2 halves. Chess ~56 × 80 ≈ 4.5 KB (×2 FIF, GpuOnly).

**Why model in the instance record:** one SSBO fetch in the VS (`gl_InstanceIndex`). Alternative: `uint transform_index` (4 B) + fetch `worlds[i]` — saves 64 B × visible, extra dependent load. At 56 instances it is noise; at 10k+ it is real.

### 3.3 `GpuCullItem` — 64 B, HostWrite, once at `build_scene` + worlds update

```
vec4  aabb_min;  // xyz local AABB
vec4  aabb_max;
uvec4 meta;      // material, batch, instance_base, capacity
uvec4 skin;      // joint_base, joint_count, material flags, transform_index
```

One per render-mesh. Local AABB + `worlds[transform_index]` → world AABB on GPU (avoids CPU transforming every box). `.w` of the vec4s is unused (std430 vec3 would still pad to 16).

---

## 4. Materials and textures

### 4.1 `gfx::Material` — 256 B, std430, binding 2

| Offset | Contents |
|--------|----------|
| 0–15 | albedo |
| 16–31 | roughness, metallic, normalStrength, clearcoat |
| 32–47 | emissive RGB + strength |
| 48–79 | 5 texture indices + flags + packed UV sets |
| 80–95 | alpha cutoff, coat rough, transmission, iridescence |
| 96–111 | iridescence ior/thickness + 8 B pad |
| 112–191 | `uv_scale_offset[5]` (KHR_texture_transform) |
| 192–223 | `uv_rotation[5]` + 12 B pad |
| 224–255 | `_tail[8]` **unused** |

**Why 256:** one aligned record for PBR + multi-UV + KHR_texture_transform + a few extensions, no indirection in `pbr.frag`. Count is small (tens), so **256 × N is not a bandwidth problem**. The 32 B tail and rotation pad are the obvious shrink if we ever care (→ 192 or 128).

Load-time `update_buffers` + `toggle`. Not per-frame.

### 4.2 Bindless textures — binding 11, max 10 000 descriptors

Images live on the GPU after the first upload (typically RGBA8 + mips via TextureManager). Descriptor table is large; **texture texels** dominate if the scene is atlas-poor.

Dummy 1×1 textures exist so unused slots stay valid.

---

## 5. Lighting / IBL / shadows (memory-heavy)

### 5.1 `FrameConstants` — UBO 432 B, std140, binding 0, 2× FIF

| Bytes | Field | Why |
|-------|--------|-----|
| 16 | `cameraPosition` (w = exposure) | Shade + auto-exposure |
| 16 | `lightMeta.x` = count | Loop bound |
| 144 | `shCoefficients[9]` | 3-band SH irradiance (l2) |
| 16 | `iblIndices` | Currently just “IBL ready”; cube/LUT are fixed bindings 7–8 |
| 192 | `shadowViewProj[3]` | Nested CSM |
| 16 | `shadowSplits` | s0, s1, far, cascade count |
| 16 | `shadowParams` | texel, enabled, light index, bias |
| 16 | `cameraForward` | Cascade pick (planar view depth) |

**Why vec4-only:** std140 scalar arrays pad to 16 B per element; this struct is memcpy-identical to GLSL.

SH (144 B) and three `mat4`s (192 B) are the bulk. Could pack SH as 27 floats in an SSBO; UBO size is irrelevant at 432 B.

Written every frame (`write_frame_lighting`). Lights SSBO (below) only when dirty.

### 5.2 `GpuLight` — 64 B × 8 = 512 B, std430, binding 6

`position, direction, colorIntensity, params` (type, range, cos inner/outer). `MAX_LIGHTS = 8` is the forward loop cap (TCF not started). Compacting this does not matter until light count jumps.

### 5.3 IBL (load, tiny)

| Resource | Format | Size |
|----------|--------|------|
| Prefiltered cube | 32² × 6 faces × ~4 mips, `R16G16B16A16_SFLOAT` | ~50 KB |
| BRDF LUT | 128² `R16G16_SFLOAT` | 64 KB |

Already small. Binding 7–8, not bindless.

### 5.4 Shadow CSM — **largest GPU image**

`kShadowCascades = 3`, `D32` (or whatever `depth_format` is), desktop **2048²**, Adreno cap **1024²**.

| Res | Texels × 3 × 4 B |
|-----|------------------|
| 2048 | **48 MB** |
| 1024 | **12 MB** |

Written every frame (GPU). Sampled as `sampler2DArrayShadow`. Disjoint (slightly overlapping) cut pyramids; near casters reach farther maps via the caster volume.

**Compaction:** 16-bit depth if the format is supported for compare; 1024 on desktop; 2 cascades. Biggest memory win in the engine if we need it.

### 5.5 Hi-Z pyramid (optional, `occlusionCull`)

`R32G32_SFLOAT` (min/max), ~full prepass extent, **2 FIF**, mips to 1×1. At 1920×1080: mip0 ≈ 16 MB **per frame slot** before mips. **RG16F** would halve it. Off on Adreno / default desktop.

---

## 6. Culling / meshlets / indirect (compute → draw)

### 6.1 `GpuCullGlobals` — UBO 464 B, std140

6 frustum planes (96) + counts + `mat4 view_proj` + HZB info + emit filter + **`extra_planes[16]` (256 B)** for the directional caster volume (`shadow-caster.md`).

Uploaded **per dispatch** (3 shadow layers + camera prepass/shade ± transparent). Still < 3 KB/frame.

One UBO is reused per FIF/pass. It **must** be written on the GPU timeline (`vkCmdUpdateBuffer`) so cas0 / cas1 / cas2 / camera each see their own planes. A host `memcpy` during recording leaves every dispatch with the last upload (camera frustum), which drops off-screen casters — `shadow-caster.md` §11.

`extra_planes` is unused on the camera shade cull (count = 0) but still in the struct so all compute shaders share one layout.

### 6.2 `GpuBatchMeta` — 32 B × batches, static HostWrite

`base, capacity, index_count, first_index, vertex_offset, meshlet_offset, meshlet_count, flags`. One per unique mesh. Tight.

### 6.3 `MeshletDesc` — 64 B, static

```
uint first_index, index_count, pad0, pad1;  // 8 B pad
vec4 cone_apex_cutoff;
vec4 cone_axis_radius;  // axis + sphere radius
vec4 sphere_center;     // .w unused
```

~64 B × meshlet count. A few thousand meshlets → hundreds of KB, load once. `pad0/pad1` and `sphere_center.w` are free bytes (could pack `first_index/index_count` + cone in 48 B).

### 6.4 Indirect commands

`VkDrawIndexedIndirectCommand` = **20 B**. GpuOnly.  
- Batch path: `batch_count × 20`  
- Meshlet path: `max_meshlet_draws × 20` (worst-case visible × meshlets)

Count buffer: 4 B, HostWrite so the CPU can read stats.

### 6.5 Meshlet cull push — 32 B

`camera_world, max_draws, cone_enable` + pad.

---

## 7. Skin

| Buffer | Stride | Cadence |
|--------|--------|---------|
| IBM | `mat4` × joints | Load |
| Joint meta | `uvec4` × joints (transform idx, mesh idx, palette out) | Load |
| Palette | `mat4` × joints, **max 256** | GPU compute each frame from `worlds[]` |

Palette = `inv(meshWorld) * jointWorld * IBM`. VS: `world = instance.model * palette * v`. Dual matrices (instance model **and** inv mesh in the palette) are the glTF convention (mesh node × skin).

256 joints × 64 B = **16 KB**. Affine 3×4 would save 25% and is the usual mobile trick; inverse of mesh world still wants a 3×3.

---

## 8. Overlays (FIF, HostWrite)

| Pass | Vertex | Notes |
|------|--------|--------|
| HUD | `vec2 pos, vec2 uv, vec4 color` = **32 B** | 8×8 atlas 128×48 R8, CPU-packed quads each frame |
| Debug lines (F3) | `vec3 pos + RGBA8` = **16 B** | Jolt debug can be large if dense |

---

## 9. Push constants (not buffers, still “to the GPU”)

| Pipeline | Size | Contents |
|----------|------|----------|
| PBR / shadow / depth | 80 B | `mat4 viewProj` + `uvec4 extra` (unused) |
| Meshlet cull | 32 B | camera + flags |
| Skin palette | 8 B | `joint_count, world_count` |

`PbrPushInstanced.extra` is 16 B reserved — drop if we ever hit the 128 B PC limit (we do not).

---

## 10. Compaction candidates (impact vs risk)

Ranked for **this** engine (Quest DRAM + current CPU wait). Do not shrink UBOs of a few hundred bytes unless it simplifies std140.

| Priority | Change | Saves | Risk |
|----------|--------|-------|------|
| **A** | **uint16 indices** where `vertex_count ≤ 65536` | ~50% of those primitives’ IB | **Landed:** mixed UINT16/UINT32 IBs; primitives with more verts stay u32 |
| **B** | **Single GpuOnly VB/IB** for non-morph meshes | 2× → 1× geometry RAM; morph keeps a mapped patch | Buffer split by morph vs static |
| **C** | **Morph: write pos/normal only** (or a compact stream) | 64 B → ~24 B per morph vert, once not twice | Extra stride / pipeline |
| **D** | **Shadow 1024² or D16** | 48 MB → 12 MB (res) or 24 MB (D16) | Quality; compare-format support |
| **E** | **HZB `RG16F`** | ~½ of pyramid | Precision of conservative test |
| **F** | **`worlds[]` 3×4 affine** + skip static nodes | ~25% of per-frame transform upload | Inverse in skin compute; static bit |
| **G** | **`DrawInstanceGPU` without `mat4`** (index into `worlds[]`) | 80 → 16 B × visible | Extra VS SSBO fetch; Adreno vs bandwidth |
| **H** | Drop vertex `_pad` / unused UV1 | 64 → 56–60 B | Alignment, weld key, VS layout |
| **I** | Material 256 → 192 (drop `_tail` + pack rotations) | ~25% of a tiny buffer | Shader churn, zero frame win |
| **J** | MeshletDesc 64 → 48 | Small static SSBO | Packing pain |
| **K** | Stop binding VB as SSBO (4) if unused | Descriptor only | Confirm no shader still reads it |

**Do not chase:** `FrameConstants` 432 B, `GpuLight` 512 B, `GpuCullGlobals` 464 B, IBL, HUD atlas.

**A+B+D** are the only items that move **megabytes**. **C+F** move **per-frame** CPU stores (morph + every glTF node’s mat4). **G** only pays off at large instance counts.

---

## 11. Suggested measurement before compacting

Tracy (`AERO_TRACY=ON`): `cpu.uploads` is worlds + lighting + morph + HUD. `cpu.gpu-wait` says whether we are GPU-bound (then shrink **shadow/HZB/fetch**, not UBO padding).

If `cpu.uploads` is large: profile `sync_worlds` vs morph `write_primitive_vertices`.  
If GPU time is `gpu.opaque` / `gpu.shadow`: vertex fetch + shadow array, not 80 B instances.

---

## 12. Related docs

- Bindings table: `docs/agents/tech_context.md`  
- UMA / HostWrite vs GpuOnly: `docs/architecture/pipeline-implementation.md`  
- Shadows: `docs/architecture/lighting-implementation.md` §4.1  
- Meshlets: `docs/architecture/visibility-lod-plan.md`
