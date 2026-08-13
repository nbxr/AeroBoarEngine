# Visibility, LOD, and Distant City Plan

Canonical plan for **what is drawn** at scale: frustum / occlusion, meshlets, cluster (Nanite-like) raster, impostors, and far-field city bake.

**See also**
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
| Meshlets / cluster raster | **Not implemented** |
| Impostors / sprite LOD / city skybox bake | **Not implemented** |

Desktop: set `"occlusionCull": true` in `configuration.json` to try conservative Hi-Z. Frustum-only is the reliable default.

---

## 2. Occlusion (desktop vs Quest)

### Desktop (optional Hi-Z)

When `occlusionCull` is true and **not** an Adreno build:

1. Frustum-cull opaque writers  
2. Depth prepass (solid only)  
3. Half-res **RG32F** pyramid: **R = min Z, G = max Z**  
4. Conservative test: AABB **fully in front of near** and **fully on-screen**; mip so ~1 texel covers the rect; cull iff `z_near > hzb.g + bias`  
5. Sky / holes raise **max ≈ 1** → no cull  

No 4×4 / spread heuristics (those were the old min-only false-cull source).

### Quest / Adreno (default)

- **Frustum + GPU instance lists only** until a **GMEM-friendly** visibility path exists.  
- Do **not** add a full extra geometry prepass as the shipping Quest occlusion design.  
- Future: Hi-Z / visibility **inside the tiled main pass** (on-chip depth), or cluster vis without a DRAM prepass.

---

## 3. Meshlets (next geometry step)

**Library:** **[meshoptimizer](https://github.com/zeux/meshoptimizer)** (`meshopt_buildMeshlets`, `meshopt_computeClusterBounds`, simplify, overdraw).

- FetchContent later (do not invent a custom meshlet builder).  
- Run at **load** (and optionally offline cook) on every mesh: any glTF → meshlets.  
- Store: vertex cone, bounding sphere / AABB per meshlet, index micro-index buffer.  
- GPU: frustum + cone cull per meshlet, then instance/cluster indirect.

This is the prerequisite for cluster LOD / “Nanite-like” cuts.

---

## 4. Cluster / “Nanite-like” raster (later)

Target feel: **dense cities on Quest 3** (Population One–class: lots of geo, aggressive LOD, no full unique Nanite on mobile day one).

Phased:

| Phase | What |
|-------|------|
| A | Meshlets + cone/frustum cull (still hardware raster) |
| B | LOD tree / DAG of clusters (simplify with meshoptimizer); pick a cut per view |
| C | Software raster **only for tiny clusters** (pixel-sized); HW raster for the rest |
| D | Streaming cluster pages; not a full UE5 Nanite clone on Adreno |

Quest constraint: stay **TBDR/GMEM-aware**. Prefer one vis + shade path, not many full-res prepasses.

---

## 5. Distant city: impostors, sprites, skybox bake

**Endless city** is a **LOD stack**, not one rasterizer:

| Distance | Representation |
|----------|----------------|
| Near | Meshlets / clusters (unique geo) |
| Mid | **Impostors** (octahedral / billboard atlas per building or block) |
| Far | **Sprites** / simplified cards |
| Horizon | **Composite skybox** — bake distant city into a cubemap / lat-long; update when the player moves enough (parallax budget) |

Bake policy (later):

- GPU or offline: render far shells to a cubemap (or 2D panorama + depth).  
- Composite as sky / far pass; **do not** simulate or Hi-Z those instances.  
- Invalidate bake on large translation or time-of-day.  
- Near/mid still real (or impostor) so the horizon is a backdrop, not a pop wall.

Population One–style density on Quest = **aggressive LOD + impostors + baked far field**, not “full Nanite + full city simulation.”

---

## 6. Config / build knobs

| Knob | Meaning |
|------|---------|
| `"occlusionCull": false` | Runtime (desktop). `true` enables conservative same-frame Hi-Z. |
| `AERO_TARGET_ADRENO=ON` | CMake; forces occlusion off. Set for Quest/Android. |

Frustum pad is **relative** (1% of AABB, min 0.1 mm) — no +5 cm world slop.

---

## 7. Suggested order of work

1. **Now:** frustum default; optional conservative Hi-Z on desktop.  
2. **Next visibility:** meshoptimizer meshlets + GPU meshlet cull.  
3. **City mid:** impostor baker + distance switch.  
4. **City far:** skybox / cubemap bake + refresh.  
5. **Later:** cluster DAG + optional software raster for tiny clusters (Nanite-like).  
6. **Quest:** never re-enable extra-pass Hi-Z as the primary path; fold vis into GMEM.
