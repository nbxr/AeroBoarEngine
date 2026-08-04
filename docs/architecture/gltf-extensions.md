# glTF Khronos Extensions — Support Matrix

Canonical tracker for **which glTF 2.0 / Khronos extensions AeroBoarEngine understands**.  
Update this file when adding loader, material, or animation support.

**See also**
- `docs/agents/current_state.md` — overall status
- `docs/architecture/animation-plan.md` — node TRS / skin / morph
- `docs/architecture/physics-plan.md` — Jolt + KHR physics (planned)
- `docs/agents/tech_context.md` — stack decisions

---

## 1. Supported today (used by the desktop renderer)

| Extension / feature | Status | Notes |
|---------------------|--------|--------|
| Core glTF 2.0 mesh / materials (metallic-roughness) | **Yes** | Base color, MR, normal, emissive, AO textures |
| Core node TRS + hierarchy | **Yes** | `TransformManager` |
| Core animations (`translation` / `rotation` / `scale`) | **Yes** | `AnimationSystem` |
| Core morph weights path (`weights`) | **Yes** | CPU morph — `MorphSystem` |
| Core skins (`JOINTS_0` / `WEIGHTS_0`, IBM) | **Yes** | `SkinSystem` + VS skin |
| `KHR_lights_punctual` | **Partial** | Load + world xform + shade; no shadows |
| `alphaMode` OPAQUE / MASK / BLEND | **MVP** | MASK discard, BLEND pipeline blend; no sort |
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
| **`KHR_texture_transform`** | UV offset / rotation / scale on texture infos (often nested under other material extensions) |

**Why it does not work today**
1. Animation loader only handles `target.path` ∈ {`translation`,`rotation`,`scale`,`weights`} — **`pointer` is ignored**.
2. Materials have **no UV transform** uniforms or shader path (`KHR_texture_transform` not applied).
3. The sample also stacks many **advanced material** extensions (below); even with pointer support, visuals would still diverge until those shade.

**Future work (when prioritized)**
1. Parse `channel.target.extensions.KHR_animation_pointer.pointer` (JSON pointer into the glTF document).
2. Resolve pointer → engine binding (e.g. material slot + `textureTransform.offset/rotation/scale`).
3. Store per-texture UV transforms; sample animation into them each frame.
4. Apply transform in VS or FS when sampling bindless textures (or transform UVs in VS).
5. Optionally support pointer targets into lights / node extras as the extension allows.

---

### 2.2 Advanced PBR material extensions (not in uber-shader)

These appear on **AnimationPointerUVs** and many material samples. The engine uses a **single metallic-roughness PBR path** without these lobes:

| Extension | Purpose |
|-----------|---------|
| `KHR_materials_transmission` | Specular transmission / glass |
| `KHR_materials_volume` | Attenuation / thickness |
| `KHR_materials_specular` | Specular factor / color (beyond dielectric F0) |
| `KHR_materials_sheen` | Cloth sheen |
| `KHR_materials_clearcoat` | Clear coat layer |
| `KHR_materials_anisotropy` | Anisotropic specular |
| `KHR_materials_iridescence` | Thin-film iridescence |
| `KHR_materials_diffuse_transmission` | Diffuse transmission |
| `KHR_materials_unlit` | Unlit (extension is **required** by AnimationPointerUVs) |

**Note:** `KHR_materials_unlit` is in `extensionsRequired` for AnimationPointerUVs — a strict client may refuse the file; we still load geometry with the default PBR path (incorrect for unlit panels).

---

### 2.3 Physics authoring (planned, not loader-backed)

| Extension | Status |
|-----------|--------|
| `KHR_physics_rigid_bodies` | **Planned** — see `physics-plan.md` / VR chess plan |
| `KHR_implicit_shapes` | **Planned** |

Runtime **Jolt** exists; glTF physics load does **not**.

---

### 2.4 Other gaps (samples may hit these)

| Feature | Status |
|---------|--------|
| Sparse accessors | Not fully verified |
| Meshopt / Draco compression | Not supported |
| `EXT_mesh_gpu_instancing` | Not supported |
| Material variants (`KHR_materials_variants`) | Not supported |
| Embedded images via bufferView only | Warned / skipped in loader |
| Full transparent sort / dual opaque-blend queues | MVP blend only |
| GPU morph | CPU only |

---

## 3. Testing policy

- `assets/scenes/configuration.json` lists **all** `glTF-Sample-Assets` models for regression browsing.
- Samples that depend on **§2** extensions are expected to load (geometry may show) but **not** match Khronos reference renders until the listed work lands.
- Prefer documenting “known incomplete” here rather than silent wrong visuals.

---

## 4. Implementation order (suggested, not committed)

1. **`KHR_texture_transform`** (static) — high leverage for many assets  
2. **`KHR_animation_pointer`** targeting texture transforms — unlocks AnimationPointerUVs UV motion  
3. **`KHR_materials_unlit`** — cheap correctness for that sample’s required ext  
4. Transmission / volume / clearcoat / etc. — large shader + pipeline investment  
5. **Physics KHR** — product path for ABeautifulGame / VR chess (separate plan)

---

*Last reviewed: end of session 2026-08-03 (desktop animation + alpha MVP + morph + physics foundation era).*
