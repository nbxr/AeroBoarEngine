# Visibility and Occlusion Plan

Canonical plan for **GPU frustum / Hi-Z / Quest GMEM visibility** — *what is tested for on-screen this frame*.

Progressive **distance LOD** (meshlets → impostors → skybox) is **`docs/architecture/cascadebake-plan.md`** (`CascadeBake` / `CascadeOven`). Do not duplicate that pipeline here.

**See also**
- `docs/architecture/cascadebake-plan.md` — CascadeBake LOD, classification, jobs
- `docs/agents/tech_context.md` — Hi-Z / reverse-Z / TBDR
- `docs/architecture/pipeline-implementation.md` — Quest pass structure
- `docs/agents/current_state.md` — what ships today

---

## 1. Status today

| Path | Status |
|------|--------|
| GPU **frustum** cull + multi-draw indirect | **On** (always) |
| Same-frame **Hi-Z occlusion** | **Optional.** Config **`occlusionCull`** (default **`false`**). Extra depth prepass + RG min/max pyramid. Conservative query when on. |
| **Adreno / Quest** | CMake **`AERO_TARGET_ADRENO`** (also auto on `ANDROID`) **forces occlusion off** — extra geometry prepass is GMEM-hostile. |
| Meshlets / CascadeBake / impostors / skybox bake | **Not implemented** — `cascadebake-plan.md` |

Desktop: set `"occlusionCull": true` in `configuration.json` to try conservative Hi-Z. Frustum-only is the reliable default.

---

## 2. Occlusion (desktop vs Quest)

### Desktop (optional Hi-Z)

When `occlusionCull` is true and **not** an Adreno build:

1. Frustum-cull opaque writers  
2. Depth prepass (solid only)  
3. Half-res **RG32F** pyramid: **R = min Z, G = max Z** (reverse-Z: min = far/hole)  
4. Conservative test: AABB **fully in front of near** and **fully on-screen**; mip = `ceil(log2(rect))` so one texel covers the rect; **4-corner min(R)**; cull iff `z_close < hzb.r - bias`  
5. Sky / holes drop **min ≈ 0** → no cull. Reduce uses integer tile coverage so odd mip sizes do not drop the last row/col.

### Quest / Adreno (default)

- **Frustum + GPU instance lists only** until a **GMEM-friendly** visibility path exists.  
- Do **not** add a full extra geometry prepass as the shipping Quest occlusion design.  
- Future: Hi-Z / visibility **inside the tiled main pass** (on-chip depth), or cluster vis without a DRAM prepass.

---

## 3. Distance LOD (owned by CascadeBake)

Near / mid / far representations, automatic static-vs-dynamic classification, `CascadeOven`, and the job system live in **`cascadebake-plan.md`**.

Meshlets use **[meshoptimizer](https://github.com/zeux/meshoptimizer)** (`meshopt_buildMeshlets`). Do not invent a custom builder.

**Cluster / Nanite-like** raster (tiny-cluster software raster, DAG cuts) is a later *near* enhancement **after** meshlets exist. It does **not** replace octahedral impostors or skybox bake for mid/far city.

---

## 4. Config / build knobs

| Knob | Meaning |
|------|---------|
| `"occlusionCull": false` | Runtime (desktop). `true` enables conservative same-frame Hi-Z. |
| `AERO_TARGET_ADRENO=ON` | CMake; forces occlusion off. Set for Quest/Android. |

Frustum pad is **relative** (1% of AABB, min 0.1 mm) — no +5 cm world slop.

---

## 5. Suggested order of work

1. **Now:** frustum default; optional conservative Hi-Z on desktop.  
2. **Next (CascadeBake):** meshoptimizer meshlets + GPU meshlet/cone cull — `cascadebake-plan.md`.  
3. **Then:** impostors → skybox bake via `CascadeOven` + job system.  
4. **Later:** cluster DAG / tiny-cluster software raster (near field only).  
5. **Quest:** never ship extra-pass Hi-Z as the primary vis path; fold vis into GMEM.
