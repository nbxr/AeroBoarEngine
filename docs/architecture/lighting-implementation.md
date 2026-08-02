# Lighting Implementation Guide

Canonical description of how lighting works in AeroBoarEngine today, and what remains planned.

**See also**
- `docs/agents/current_state.md` — gaps and priorities
- `docs/agents/tech_context.md` — descriptor bindings
- `docs/architecture/pipeline-implementation.md` — pipeline / Quest vision
- `docs/agents/architecture_principles.md` — TBDR constraints

---

## 1. Current model (what ships)

Forward PBR (`pbr.vert` / `pbr.frag`) with analytic lights + procedural IBL.

### 1.1 Descriptor layout

| Binding | Type | Role |
|---------|------|------|
| 0 | UBO `gfx::FrameConstants` | Camera position + exposure (`.w`), light count, SH coeffs, IBL texture indices. Members are `vec4`/`uvec4` only (std140 matches C++ without padding hacks). |
| 6 | SSBO `gfx::GpuLight[]` | Up to `MAX_LIGHTS` (8) lights, std430 |
| 7 | `samplerCube` | Prefiltered specular env (GGX mips) |
| 8 | `sampler2D` | BRDF integration LUT |
| 9 | bindless textures | Highest binding (variable count) |

Per-frame double-buffering: `write_frame_lighting()` + `bind_frame_lighting_to_all_sets()`.

### 1.2 Light sources

- **Primary:** glTF `KHR_lights_punctual` (directional / point / spot), world transforms from the node hierarchy at load (same walk as meshes/cameras).
- **Fallback:** engine `renderer.globalLight` (directional) when the scene has no punctual lights.
- **Point lights:** KHR inverse-square falloff when `range == 0`.
- **Auto-exposure:** accounts for point-light distance vs scene center so high candela exports remain visible.

### 1.3 Shading

- Cook-Torrance GGX (D / F / G) for analytic lights.
- Diffuse + specular IBL via split-sum: SH irradiance in `FrameConstants` + prefiltered cube + BRDF LUT.
- Material path: albedo, normal (flag-controlled flip), metal/rough, emissive, separate AO / ORM as implemented in `pbr.frag`.

### 1.4 IBL (`gfx::IblEnvironment`)

Baked at engine init (procedural outdoor sky; no external HDR required yet):

- 3-band SH → `FrameConstants.shCoefficients`
- Prefiltered cubemap (e.g. 32², few mips) → binding 7
- BRDF LUT (e.g. 128²) → binding 8

---

## 2. glTF lights & transforms

- Light nodes use the same composed world matrix as mesh/camera nodes after hierarchy `propagate()`.
- **Directional** (KHR): local emission −Z; stored direction for the shader is the world-space local +Z of the node (`normalize(rot[2])`) so `L` matches `NdotL`.
- **Point / spot** position: translation of the world matrix; rotation/scale ignored for position (per KHR).
- Policy: **strict glTF fidelity** — no global exporter correction matrices. Fix assets at export if needed.

---

## 3. Known limitations (current)

- Fixed light count (`MAX_LIGHTS = 8`); no clustered/tiled many-lights.
- Lights are largely **static after load** (no full dynamic mutation / dirty path yet).
- **Spot** direction / cone packing is incomplete (see packing in `Light.h` / shader).
- No shadows (`PassType::Shadow` scaffolding only).
- No HDR equirect env load path yet (procedural IBL only).
- Lights live as a flat list on `Renderer`, not yet first-class GameObject components.

---

## 4. Next lighting work

Priority order aligns with `current_state.md`:

1. Complete spot direction packing and cone math  
2. HDR equirect environment load into the existing IBL pipeline  
3. Dynamic light mutation (dirty tracking + safe double-buffer updates)  
4. Larger light counts / clustering suitable for Quest  
5. Shadows and richer GI as later phases  

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
