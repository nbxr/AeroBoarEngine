# glTF Animation Implementation Plan

Canonical plan for bringing glTF animation online. **Not implemented yet** — implement against this doc next session.

**See also**
- `docs/agents/current_state.md` — status / next immediate
- `docs/architecture/game-object-implementation.md` — hierarchy + `TransformManager`
- `docs/agents/tech_context.md` — pipeline timing notes

---

## 1. Current state (what is true today)

| Piece | Status |
|--------|--------|
| tinygltf parse (file may contain `animations` / `skins`) | Yes |
| Load or store animation clips | **No** |
| Sample channels over time | **No** |
| Hierarchy + dirty `propagate` | Yes |
| Per-frame world → cull/draw (`sync_scene_transforms`) | Yes |
| Durable `gltf_node → transform_index` after load | **No** (map is local in `load_scene` and discarded) |
| Decomposed TRS storage on nodes | **No** (matrix-only locals from `extract_node_transform`) |
| JOINTS/WEIGHTS from glTF into `Vertex` | **No** (defaults only) |
| Inverse bind matrices / joint palette | **No** |
| Skinning in VS / depth prepass | **No** |
| Morph targets | **No** |

**Conclusion:** Rigid **node TRS animation** can build on existing transform sync. **Skinned** animation is a second, larger project. Morphs are later.

Architecture stubs (`GameObject::skin_index`, `RenderMesh::skin_index`, `Vertex` blend fields, `ComputePassContext::animation_pipeline`) are placeholders only.

---

## 2. Two animation problems (do not conflate)

### A. Node TRS animation (Phase 1 — first milestone)

glTF channels target a **node** path: `translation` / `rotation` / `scale` (and later `weights` for morph).

Each frame:
1. Sample keyframes (interpolation)
2. Compose **local** matrix for the node
3. `TransformManager::set_local_matrix(transform_index, local)` (marks dirty)
4. Existing `Engine::sync_scene_transforms()` → worlds, dual-write instances, lights, per-frame GPU cull models

**No shader changes** if every drawn mesh is rigid under that hierarchy.  
**Test asset:** e.g. `AnimatedCube` from glTF-Sample-Assets (add to `configuration.json` when implementing).

### B. Skinned mesh animation (Phase 2)

Requires Phase 1 for **joint** nodes, plus:
- Load `skins` (joints list, `inverseBindMatrices`)
- Load `JOINTS_0` / `WEIGHTS_0` into vertices
- Joint matrix palette: `jointMatrix[j] = world(joint[j]) * inverseBind[j]` (match glTF skinning formula / mesh root convention)
- Skin in **`pbr.vert` and depth-prepass VS** (must match or Hi-Z / occlusion diverge)
- Cull AABB policy (inflate, bone AABBs, or recompute) — rigid mesh-local AABB is wrong under large deformation

### C. Morph targets (Phase 3+)

Blend-shape targets on primitives — separate from node/skin; schedule after A/B.

---

## 3. Phase 1 work breakdown (implement next)

### 3.1 Persist glTF ↔ engine maps at load

Today `node_to_xform` dies at end of `load_scene`. Keep:

- `std::vector<uint32_t> gltf_node_to_transform` (size = node count; `TransformManager::kInvalid` if none)
- Optional reverse map for debugging
- Ensure **all** scene-graph nodes that can be animation targets (including empty/joint-only nodes) have a transform slot — full tree walk already aims at this; verify when implementing

Own this on `SceneManager` or a small `scene::AnimationSet` / scene animation registry filled at load.

### 3.2 Prefer decomposed TRS on animated nodes

glTF anim often updates **one path** (e.g. rotation only). Matrix-only locals force awkward recomposition.

- Store rest **T / R / S** per transform (or only for nodes that are animation targets)
- On sample: update component(s), rebuild local = `T * R * S`, then `set_local_matrix`
- Matrix-only glTF nodes (no TRS keys): treat carefully if a channel targets T/R/S

### 3.3 Load animation clips from tinygltf

Structures (names illustrative):

```
AnimationClip
  name, duration
  channels[] → { target (gltf node or transform index), path (T|R|S), sampler_index }
  samplers[]  → { times[], values[], interpolation (STEP|LINEAR|CUBICSPLINE) }
```

- Read `model.animations` after hierarchy exists so targets resolve
- **Interpolation minimum:** `LINEAR` + `STEP`; add `CUBICSPLINE` if sample assets require it

### 3.4 Playback

- `AnimationPlayer` / instance: clip id, time, speed, loop, playing
- Advance time in the main loop (with camera/input), **before** `render()` (or at start of frame before sync)
- Sample → write locals → rely on existing fence-gated `sync_scene_transforms()`

Do **not** bypass dirty propagate with ad-hoc GPU buffer writes.

### 3.5 Verification (Phase 1)

- [ ] Build + shaders still clean
- [ ] Animated sample scene listed in `configuration.json`
- [ ] Motion visible under GPU cull + depth prepass + shade
- [ ] No validation errors when transforms change every frame
- [ ] Lights parented to animated nodes (if any) still update via `transform_index`

---

## 4. Phase 2 work breakdown (skinned — later)

| Item | Notes |
|------|--------|
| JOINTS/WEIGHTS load | Fill existing `Vertex::blend_indices` / `blend_weights`; normalize weights |
| Pipeline vertex attrs | Bind skin attributes in graphics + depth-prepass pipelines |
| Skin registry | Joint transform indices + IBM buffer |
| Joint palette SSBO | Bind in VS; rebuild after joint worlds update each frame |
| Skinning in VS | Both shade and depth prepass |
| AABB policy | Document chosen strategy; implement with cull |

`skin_index` on GO/RM becomes real. Optional later: GPU skinning compute (Quest bandwidth); first implementation may be CPU palette + VS skin.

---

## 5. Engine constraints (do not violate)

1. **Mutate locals only via dirty path** — `set_local_matrix` / TRS helpers + `sync_scene_transforms`.
2. **Depth prepass must match shade** — skinning (Phase 2) in both.
3. **FIF-safe cull models** — already handled if sync stays after frame fence wait.
4. **Animated light nodes** — work “for free” once node anim + existing light `transform_index` refresh run.
5. **CPU sample first** — docs allow future GPU anim; do not block Phase 1 on compute animation.

---

## 6. Out of scope for first milestone

- Morph targets  
- Multi-clip blend / crossfade / retargeting / IK  
- GPU animation compute  
- Changing multi-draw batching topology for anim  

---

## 7. Suggested file / module touch list (Phase 1)

| Area | Likely changes |
|------|----------------|
| `SceneManager` / load path | Persist node→transform; optional rest TRS |
| New `scene/Animation*.h|.cpp` | Clips, load from tinygltf, sample, player |
| `GltfLoader` or load_scene | Extract animations after hierarchy |
| `AeroBoar.cpp` / engine tick | Advance players before `render()` |
| `configuration.json` | Animated test scene entry |
| Docs | Mark Phase 1 done in `current_state.md` when landed |

---

## 8. Implementation order (next session)

1. Persist `gltf_node_to_transform` (+ rest TRS if needed)  
2. Load clips/channels/samplers  
3. Player + sample LINEAR/STEP  
4. Wire tick → `set_local_matrix` → existing sync  
5. Test with AnimatedCube (or similar)  
6. Only then open Phase 2 (skinning)

Update this file when phases land or decisions change (e.g. cubic spline required, TRS storage layout).
