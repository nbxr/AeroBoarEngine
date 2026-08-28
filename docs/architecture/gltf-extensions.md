# glTF Khronos Extensions — Support Matrix

Canonical tracker for **which glTF 2.0 / Khronos extensions AeroBoarEngine understands**.  
Update this file when adding loader, material, or animation support.

**See also**
- `docs/agents/current_state.md` — overall status
- `docs/architecture/animation-plan.md` — node TRS / skin / morph
- `docs/architecture/physics-plan.md` — Jolt + KHR physics (MVP load landed)
- `docs/agents/tech_context.md` — stack decisions

---

## 1. Supported today (used by the desktop renderer)

| Extension / feature | Status | Notes |
|---------------------|--------|--------|
| Core glTF 2.0 mesh / materials (metallic-roughness) | **Yes** | Base color, MR, normal, emissive, AO textures |
| **`.glb` embedded images** (`bufferView` / no external URI) | **Yes** | Decoded via tinygltf pixels or `stbi_load_from_memory` |
| Vertex colors (`COLOR_0`) | **Yes** | RGBA8 unorm in vertex (VEC3/VEC4 float or unorm); multiplies albedo; default white |
| Multi-UV (`TEXCOORD_0` / `TEXCOORD_1`) | **Yes** | Vertex packs both; material picks set per texture |
| `KHR_texture_transform` | **Yes (MVP)** | Static scale / offset / rotation at load (animated needs animation_pointer) |
| `KHR_materials_clearcoat` | **Yes (MVP)** | Factor + roughness; second specular/env lobe (no clearcoat maps yet) |
| `KHR_materials_emissive_strength` | **Yes** | Multiplies emissive RGB factor |
| `KHR_materials_transmission` | **MVP** | Factor + blend alpha + env refraction proxy (no full refraction/MRT) |
| `KHR_materials_iridescence` | **MVP** | Simplified thin-film F0 tint (not full spectral BRDF) |
| `KHR_materials_variants` | **Partial** | Applies **variant index 0** at load only (no runtime switch UI) |
| Core node TRS + hierarchy | **Yes** | `TransformManager` |
| Core animations (`translation` / `rotation` / `scale`) | **Yes** | `AnimationSystem` |
| Core morph weights path (`weights`) | **Yes** | CPU morph — `MorphSystem` |
| Core skins (`JOINTS_0` / `WEIGHTS_0`, IBM) | **Yes** | `SkinSystem` + VS skin |
| `KHR_lights_punctual` | **Partial** | Load + world xform + shade; **one directional shadow map** (no point cubes / spot atlas yet) |
| `alphaMode` OPAQUE / MASK / BLEND | **Yes (MVP)** | Dual pipelines: opaque depth-write on; BLEND/transmission depth-write off. No transparent sort |
| Non-indexed primitives | **Yes** | Synthetic indices (e.g. Fox) |
| Missing NORMAL / TEXCOORD | **Yes** | Defaults + optional face normals |

---

## 2. Not supported (explicit backlog)

### 2.1 Animation pointer + texture UV animation

**Sample that fails to render “as intended”:**  
`AnimationPointerUVs` (`glTF-Sample-Assets/Models/AnimationPointerUVs/…`)

| Extension | Role in that asset |
|-----------|-------------------|
| **`KHR_animation_pointer`** | Animation channels target JSON pointers (not only node TRS). Here: animated **texture transform** offsets/rotations/scales on many materials |
| **`KHR_texture_transform`** | UV offset / rotation / scale on texture infos (static transforms **supported** at load; **animated** transforms need pointer) |

**Why it does not work fully today**
1. Animation loader only handles `target.path` ∈ {`translation`,`rotation`,`scale`,`weights`} — **`pointer` is ignored** (so animated UV transforms never update).
2. Static `KHR_texture_transform` + multi-UV **are** applied at load/sample (see Material UV fields).
3. The sample also stacks many **advanced material** extensions (below); visuals still diverge until those shade.

**Future work (when prioritized)**
1. Parse `channel.target.extensions.KHR_animation_pointer.pointer` (JSON pointer into the glTF document).
2. Resolve pointer → engine binding (e.g. material slot + live `textureTransform.offset/rotation/scale`).
3. Sample animation into those fields each frame (static path already exists).
4. Optionally support pointer targets into lights / node extras as the extension allows.

---

### 2.2 Advanced PBR material extensions (not in uber-shader)

These appear on **AnimationPointerUVs** and many material samples. The engine uses a **single metallic-roughness PBR path** without these lobes:

| Extension | Purpose |
|-----------|---------|
| `KHR_materials_volume` | Attenuation / thickness (not loaded) |
| `KHR_materials_dispersion` | Wavelength-dependent IOR / chromatic refraction (not loaded) |
| `KHR_materials_ior` | Non-default IOR for refraction (not loaded; shade uses fixed ~1.5 proxy) |
| `KHR_materials_specular` | Specular factor / color beyond dielectric F0 |
| `KHR_materials_sheen` | Cloth sheen |
| `KHR_materials_anisotropy` | Anisotropic specular |
| `KHR_materials_diffuse_transmission` | Diffuse transmission |
| `KHR_materials_unlit` | Unlit (required by AnimationPointerUVs) |
| Clearcoat/transmission/iridescence **textures** | Factor-only MVP; maps ignored for now |
| Full transmission / volume glass | Transparent pass exists (depth-write off); still no refraction RT / volume absorption |
| Runtime material variants UI | Only variant 0 applied at load |

**Note:** `KHR_materials_unlit` is in `extensionsRequired` for AnimationPointerUVs — a strict client may refuse the file; we still load geometry with the default PBR path (incorrect for unlit panels).

#### Volume / dispersion / thick glass — deferred (desktop-only quality tier later)

**Decision (2026-08):** Do **not** implement full `KHR_materials_volume`, `KHR_materials_dispersion`, or multi-sample refraction for the current product path (ECS, Player, Quest). Showcase assets such as **`DragonDispersion`** (`transmission` + `volume` + `dispersion` + `ior`, thickness maps, ~140k verts) will load geometry but **will not** match Khronos reference until a dedicated glass tier exists.

**Why skip for now**
- Correct look needs opaque background → refraction sampling, thickness/absorption, and (for dispersion) multi-wavelength IOR — high bandwidth/ALU, poor fit for default **Quest 3 / TBDR** budgets and multiview.
- Thin-wall transmission MVP (CarConcept glass) is already enough for product content; thick gem glass is a beauty/demo feature.

**Middle path (when prioritized — not started)**
1. **Parse only:** load IOR / volume (attenuation, thickness factor/texture) / dispersion factors into `gfx::Material` (or a side table) so the matrix stays honest and assets are data-ready.
2. **Shader stub:** default path keeps today’s cheap transmission/env proxy; no extra RT cost.
3. **Desktop quality tier (optional):** enable a real refraction + volume (+ optional dispersion) path behind a **desktop-only** quality flag / `#define` / config bit; **force off on mobile/Quest** builds so shipping VR never pays for it.
4. Do **not** make dispersion the default desktop path either — opt-in showcase quality.

**Known incomplete samples (glass tier):** `DragonDispersion`, `DispersionTest`, `CompareDispersion`, `CompareVolume`, and several Transmission\* demos that need more than thin-wall MVP.

---

### 2.3 Physics authoring (MVP loader-backed)

| Extension | Status |
|-----------|--------|
| `KHR_physics_rigid_bodies` | **MVP** — `spawn_scene_physics`; see `physics-plan.md` |
| `KHR_implicit_shapes` | **MVP** — box / capsule; sphere≈box; mesh → convex hull |

Runtime **Jolt** + KHR spawn path. Not yet: compounds, triangle meshes, full filter systems.

---

### 2.4 Other gaps (samples may hit these)

| Feature | Status |
|---------|--------|
| Sparse accessors | Not fully verified |
| Meshopt / Draco compression | Not supported |
| `EXT_mesh_gpu_instancing` | Not supported |
| Material variants UI (runtime switch) | Load applies index 0 only |
| Embedded images via bufferView only | Warned / skipped in loader |
| Transparent sort / OIT | Dual queues yes; no sort / OIT |
| GPU morph | CPU only |

---

## 3. Testing policy

- `assets/scenes/configuration.json` lists **all** `glTF-Sample-Assets` models for regression browsing.
- Samples that depend on **§2** extensions are expected to load (geometry may show) but **not** match Khronos reference renders until the listed work lands.
- Prefer documenting “known incomplete” here rather than silent wrong visuals.
- **CarConcept** is a good smoke test for multi-UV, texture transform, transmission glass, emissive maps (`Dash_E` / `Khronos_C`), and opaque/transparent split.
- **DragonDispersion** / volume+dispersion samples: geometry may show; full glass is **out of scope** until the desktop-only middle path in §2.2.

---

## 4. Implementation order (suggested, not committed)

1. **`KHR_animation_pointer`** targeting texture transforms — unlocks AnimationPointerUVs UV motion  
2. **`KHR_materials_unlit`** — cheap correctness for that sample’s required ext  
3. Clearcoat/iridescence maps; variants UI  
4. **Optional desktop glass tier** (parse → stub → desktop refraction/volume; off on Quest) — §2.2  
5. **Physics KHR polish** — compounds / filters; FPS controller; VR chess (separate plans)

---

*Last reviewed: 2026-08-04 (volume/dispersion deferred; desktop-only glass middle path noted).*
