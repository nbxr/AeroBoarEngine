# Lighting Implementation Guide

Canonical description of how lighting works in AeroBoarEngine today, and what remains planned.

**See also**
- `docs/agents/current_state.md` — gaps and priorities
- `docs/agents/tech_context.md` — descriptor bindings
- `docs/architecture/pipeline-implementation.md` — Quest pipeline; TCF / clustered lights still aspirational
- `docs/agents/architecture_principles.md` — TBDR constraints

---

## 1. Current model (what ships)

Forward PBR (`pbr.vert` / `pbr.frag`) with analytic lights + procedural IBL.

### 1.1 Descriptor layout

| Binding | Type | Role |
|---------|------|------|
| 0 | UBO `gfx::FrameConstants` | Camera + exposure, light count, SH, IBL indices, shadow VP. `vec4`/`uvec4`/`mat4` only (std140). |
| 6 | SSBO `gfx::GpuLight[]` | Up to `MAX_LIGHTS` (8) lights, std430 |
| 7 | `samplerCube` | Prefiltered specular env (GGX mips) |
| 8 | `sampler2D` | BRDF integration LUT |
| 9 | joint SSBO | Skin palettes |
| 10 | `sampler2DShadow` | Directional shadow map |
| 11 | bindless textures | Highest binding (variable count) |

Per-frame double-buffering: `write_frame_lighting()` + `bind_frame_lighting_to_all_sets()`.

### 1.2 Light sources

- **Primary:** glTF `KHR_lights_punctual` (directional / point / spot), world transforms from the node hierarchy at load (same walk as meshes/cameras).
- **Fallback:** engine `renderer.globalLight` (directional) when the scene has no punctual lights.
- **Point lights:** KHR inverse-square falloff when `range == 0`.
- **Spot lights:** world **position** + emission **direction** (node −Z); cone params packed as `cos(inner)` / `cos(outer)`; falloff via `smoothstep`.
- **GPU record (`GpuLight`, 64 B):** `position`, `direction`, `colorIntensity`, `params` (type, range, cosInner, cosOuter).
- **Auto-exposure:** accounts for point/spot distance vs scene center so high candela exports remain visible.
- **Dynamic:** `write_frame_lighting()` every frame; mutate `renderer.lights` or use `Engine::set_light` / `add_light` / `set_light_enabled`. After hierarchy `propagate()`, call `refresh_lights_from_transforms()` for lights that stored a `transform_index` at load.

### 1.3 Shading

- Cook-Torrance GGX (D / F / G) for analytic lights.
- Diffuse + specular IBL via split-sum: SH irradiance in `FrameConstants` + prefiltered cube + BRDF LUT.
- Material path: albedo, normal (flag-controlled flip), metal/rough, emissive, separate AO / ORM as implemented in `pbr.frag`.

### 1.4 IBL (`gfx::IblEnvironment`)

Baked at engine init:

- Default: procedural outdoor sky
- Optional: Radiance **`.hdr` equirect** via `configuration.json` → `"environmentHdr": "path/to/map.hdr"` (absolute, or relative to active `home` path)
- 3-band SH → `FrameConstants.shCoefficients`
- Prefiltered cubemap (e.g. 32², few mips) → binding 7
- BRDF LUT (e.g. 128²) → binding 8

---

## 2. glTF lights & transforms

- Light nodes use the same composed world matrix as mesh/camera nodes after hierarchy `propagate()`.
- **Directional** (KHR): emission −Z; stored **to-light** `direction` = world +Z (`normalize(rot[2])`) for `NdotL`.
- **Point:** `position` = translation of world matrix.
- **Spot:** `position` = translation; `direction` = emission = world −Z (`normalize(-rot[2])`).
- Policy: **strict glTF fidelity** — no global exporter correction matrices. Fix assets at export if needed.

---

## 3. Known limitations (current)

- Fixed light count (`MAX_LIGHTS = 8`); shade loops every light. No tiled/clustered many-lights yet.
- Shadows: directional **CSM** (`ShadowMap` 2D array) + **silhouette-plane receiver culling** (Seb Aaltonen / DOTS-style). No point cubes or spot atlas yet.
- Lights live as a flat list on `Renderer`, not yet first-class GameObject components.
- HDR bake is CPU-side at init (not runtime hot-swap without re-init).

---

## 4. Next lighting work

1. **Investigate Tiled Clustered Forward (TCF) for mobile / Quest lighting** (not started).  
   Today `pbr.frag` loops up to `MAX_LIGHTS` (8) per pixel. TCF (tiled + clustered / Forward+ style light lists: 2D tiles × depth bins, compute cull, shade only local lights) is the candidate to raise light counts without a fat G-buffer. **Why it matters on Adreno 740:** TBDR wants lighting in the same tile/subpass; extra deferred attachments and full-screen DRAM round-trips are expensive. Any TCF design must stay GMEM-friendly (compute light lists outside the shade RP, small per-tile lists, no extra stored G-buffer). Scope before implementing: binning resolution, reverse-Z Z-bins, stereo/multiview, transparents/WBOIT, and whether 8 lights is enough until VR chess needs more.  
2. **Directional shadows** — camera fit + silhouette caster cull + 3 cascades (see §4.1). Spot atlas later. No contact hardening / PCSS until profiled.  
3. Runtime IBL hot-reload / higher-res prefilter when needed  

---

## 4.1 Directional shadows

Light-space reverse-Z ortho, texel-snapped. **Shared by both eyes later** (cyclops / union frustum — never per-eye maps).

- First enabled directional (KHR or `globalLight`) casts. Other lights unshadowed. IBL unshadowed. Blend/transmission do not **cast**.
- Frame per cascade: GPU instance cull (opaque, **whole-mesh**) → depth into array layer → then camera cull overwrites instances for shade.
- Binding 10: `sampler2DArrayShadow`. `FrameConstants`: `shadowViewProj[3]`, `shadowSplits`, `shadowParams`, `cameraForward`.
- Compare `GREATER_OR_EQUAL`; clamp-to-border 0. **8-tap Vogel PCF**. Skip when `NdotL <= 0` or sample off-map.
- Config: `"shadows": { "enabled", "resolution" (2048 desktop / 1024 Adreno cap), "bias" }`.
- Do **not** add a full-screen shadow mask pass (GMEM).

### Phase 1 (landed) — camera-fitted map

Do **not** fit to the whole scene AABB.

1. World corners of the **padded camera frustum** (or cascade slice).
2. **Extrude toward the light**, clip to scene AABB.
3. `fit_view_proj` → ortho **rectangle** (the map). GPU cull uses those 6 planes so casters outside the map are dropped.

### Phase 1b (landed) — silhouette receiver culling (Aaltonen)

The ortho rectangle still contains casters that cannot hit any **receiver**. Seb Aaltonen / Unity DOTS:

1. Cascade (or full camera) **cut-pyramid** = receiver volume.
2. Drop front faces; 2D convex hull of the 8 corners as seen along `to_light`.
3. Each hull edge + light direction → extra clip plane (`GpuCulling` `extra_planes[8]`).
4. Shadow `cull_frustum` tests instance AABBs against ortho **and** these planes.

Looking along the light (degenerate hull): extra count = 0, ortho-only fallback.

### Phase 2 (landed) — three cascades

Same fit + silhouette **per camera-depth split**.

| | Desktop | Quest cap |
|--|---------|-----------|
| Cascades | 3 | 3 |
| Size | `texture2DArray`, config resolution (Adreno cap 1024) | same cap |
| Splits | practical λ = 0.7, view-space distance along camera forward | same |
| Stereo later | one cyclops / union frustum | same |

Cascades are **nested** (0..s0, 0..s1, 0..far) so a near caster (Barbarian) still rasterizes into farther maps; disjoint slices cut umbras at the split. `pbr.frag` picks by view-depth and **blends** in an 18% band. Silhouette planes use the same nested pyramid.

### Phase 3 (later)

Static far-cascade cache (CascadeBake). Four cascades / PCSS / SDSM / extra mask: not until profiled. 

---

## 5. Quest 3 notes

- Prefer precomputed IBL (SH + short mip chain, few specular taps).
- Keep lighting in the same subpass/tile where possible; avoid extra full-screen DRAM round-trips.
- Non-uniform texture indexing and heavy per-pixel branches cost on Adreno — keep control flow uniform when possible.

---

## 6. Verification checklist

- Build + `compile_shaders` succeed.
- Scenes with `KHR_lights_punctual` show plausible multi-light GGX; scenes without lights use global directional + IBL.
- Multi-material assets: metal/rough response, emissive, AO still correct.
- No reliance on removed debug visualization modes in `pbr.frag`.

---

## References

- Real-Time Rendering 4th ed. (microfacet BRDF)
- Karis, “Real Shading in Unreal Engine 4” (split-sum IBL)
- glTF `KHR_lights_punctual`
- Qualcomm Adreno / Meta Quest Vulkan best practices

Update this file when the live lighting layout or load path changes in a lasting way.
