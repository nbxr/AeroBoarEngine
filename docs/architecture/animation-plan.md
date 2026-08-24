# glTF Animation Implementation Plan

Canonical plan for glTF animation.

**Status:** **Phase 1 (node TRS), Phase 2 (skinned meshes), and Phase 3 morph (CPU) landed.**  
Morph: `MorphSystem` loads target deltas, animation path `weights`, CPU blend into mesh VBs. Test: **AnimatedMorphCube**.

**See also**
- `docs/agents/current_state.md` — status / next immediate
- `docs/architecture/game-object-implementation.md` — hierarchy + `TransformManager`
- `docs/agents/tech_context.md` — pipeline timing notes
- `docs/architecture/gltf-extensions.md` — extension support matrix (incl. **KHR_animation_pointer**)
- `tools/blender/src/animation_transfer/` — Blender addon: copy clips between matching armatures, scale location to dest size
- `tools/blender/src/ecs_components_editor/` — Blender addon: `ECS_Components_v1` extras UI

---

## 1. Current state (what is true today)

| Piece | Status |
|--------|--------|
| tinygltf parse | Yes |
| Load / store / sample node TRS clips | **Yes (Phase 1)** — `scene::AnimationSystem` |
| Hierarchy + dirty `propagate` | Yes |
| Per-frame world → cull/draw (`sync_scene_transforms`) | Yes |
| Durable `gltf_node → transform_index` | **Yes** — `SceneManager::gltf_node_to_transform()` |
| Decomposed TRS on nodes | **Yes** — `TransformManager::LocalTrs` + set_local_translation/rotation/scale |
| Clip playback | **Crossfade** (`crossfade`, N uses 0.2 s fade); exclusive still used for load / fade=0 |
| JOINTS/WEIGHTS / skins / VS skinning | **Yes (Phase 2)** — `SkinSystem` + `pbr.vert` / prepass |
| Joint palette build | **GPU compute** (`skin_palette.comp`) from `worlds[]` + static IBM; CPU fallback if compute fails |
| Mesh-relative skin matrices | **Yes** — `inv(meshWorld) * jointWorld * IBM`, VS multiplies mesh model |
| Missing NORMAL (e.g. Fox) | **Yes** — face-normal accumulation at load |
| Morph targets | **Yes (CPU Phase 3)** — `MorphSystem`; path `weights`; POSITION+NORMAL deltas |
| `KHR_animation_pointer` | **No** — channels with `path: "pointer"` ignored |
| `KHR_texture_transform` | **Static yes** — load-time scale/offset/rotation on materials; **animated** needs animation_pointer |

**Code:** `src/scene/Animation.{h,cpp}`, `src/scene/Skin.{h,cpp}`, `src/scene/Morph.{h,cpp}`, TRS on `TransformManager`, load in `Engine.InitializeScene`, tick `Engine::update_animations` from `AeroBoar.cpp`.

**Test scenes**: skinned demos + **`AnimatedMorphCube`** for morph weights.

**Known incomplete sample:** **`AnimationPointerUVs`** needs **`KHR_animation_pointer`** (static texture transform already works) plus advanced materials (unlit, etc.). Geometry may load; animated UVs will **not** match reference. Tracked in `docs/architecture/gltf-extensions.md` §2.1.

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

### C. Morph targets (Phase 3 — CPU landed)

Blend-shape targets on primitives:
- Load `primitives[].targets[]` POSITION (+ NORMAL) deltas into `MorphSystem`
- Animation channel path `weights` (SCALAR × target count per key) → `set_weights`
- Each frame: `position = base + Σ w_i * delta_i` written into MeshManager VBs
- **Future:** GPU morph in VS / compute for larger assets

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

- [x] Build + Phase 1 code landed
- [x] `AnimatedCube` listed in `configuration.json` (use as default to see motion)
- [ ] Motion visible under GPU cull + depth prepass + shade (manual check)
- [ ] No validation errors when transforms change every frame
- [ ] Lights parented to animated nodes still update via `transform_index`

**Note:** `AnisotropyRotationTest` is a materials sample and has **no** `animations` array — log will show `[Anim] No node animations in this scene`.

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

## 6. Out of scope for Phases 1–3 (historical)

- Morph targets *(landed CPU)*  
- Multi-clip blend / crossfade — **Phase 4 landed** (two-clip fade; N + `LocomotionAnim`)  
- Retargeting / IK  
- GPU animation compute  

---

## 9. Phase 4 — clip crossfade + locomotion graph (**landed**)

**Goal:** player character **Idle / Walk / Run** with **smooth transitions**. Clip names come from extras (`idle` / `walk` / `run`); defaults `Idle` / `Walk` / `Run`. If Idle is missing at bind, the engine also tries **`T-Pose`** (common Mixamo-style packs). Example pack: `PlayerCharacters/glTF/Barbarian.gltf` (`T-Pose`, `Walking_A/B/C`, `Running_A/B`, jumps).

### 9.1 Engine: two-clip crossfade (**landed**)

`play_exclusive` still hard-cuts (load / fade=0). `crossfade` keeps at most two `AnimationPlayer`s:

| Field | Role |
|--------|------|
| `clip_index`, `time`, `speed`, `loop`, `playing` | as now |
| `weight` | 0–1 contribution |
| `fade_duration`, `fade_t` | outgoing fades to 0, incoming to 1 |

`update()`:

1. Advance all playing players.  
2. **Per channel / transform:** sample A and B; **lerp** translation/scale, **nlerp** rotation; `out = mix(A, B, wB)` with `wA + wB = 1`.  
3. When fade done, drop the outgoing player (back to one clip).  

N-key cycle should **crossfade** (e.g. 0.2 s) instead of exclusive cut.

**Root motion / feet on the floor:** gameplay owns the player extras node (`FpsMove` raycast Y + WASD XZ). Ignore translation on a skeleton node named **`root`** so Walk/Run stay in place on XZ. **Play hips/legs as authored** — do not clamp hips Y, lift skeleton `root`, or chase the lowest foot after sampling. Split body/leg meshes weighted to `hips` vs `upperleg.*` have those local translations keyed together; editing one bone after sample is what made the torso look like it left the legs.

Feet through the floor (or a huge COM bounce) is an **authoring** problem, not an engine plant:

1. **Retarget translations to the destination rest pose.** Animation Transfer **Duplicate** copies F-curves 1:1. If dest is a different size than source, scale `hips`/`root` location keys (addon **Scale location to dest size**), or bake with matching rest poses.
2. **In-place clips** whose lowest foot sits on the armature origin (the extras empty).
3. **Later, two-bone IK** (hips stay, feet meet the ground). Not started.

`worldScale` only amplifies a bad clip; it is not the fix. `boom_offset` is asset meters × `worldScale`.

### 9.2 Gameplay: tiny locomotion graph (**landed**)

`ecs::LocomotionAnim` on the player (extras `locomotion_anim` or typo `locomation_anim`):

```text
stand  → extras "idle"  (default "Idle"; bind also tries "T-Pose")
walk   → extras "walk"  (default "Walk")
run    → extras "run"   (default "Run")
walk_threshold, run_threshold   (horizontal speed, sim m/s)
fade_seconds                    (e.g. 0.15–0.25)
```

`LocomotionAnimSystem` (after move, before or as part of `update_animations`):

- `speed = length(horizontal velocity)` from `FpsMove` (or wish * walk_speed).  
- State: `Stand` / `Walk` / `Run` with **hysteresis** so it does not chatter.  
- On state change → `AnimationSystem::crossfade(clip, fade)`.  

Authoring: glTF extras on the player node, e.g.

```json
{ "type": "locomotion_anim",
  "idle": "Idle", "walk": "Walk", "run": "Run",
  "walk_speed": 1.2, "run_speed": 3.5, "fade": 0.2 }
```

Mixamo-style packs can use `"idle": "T-Pose", "walk": "Walking_A", "run": "Running_A"`. If extras omit names, bind uses `Idle`/`Walk`/`Run` then `T-Pose` as idle fallback.

### 9.3 Third-person camera (parallel, same slice)

**Landed (boom + look-at).** Storage option A (`scene::Camera`). `CameraRig.third_person` + `boom_offset` (right, up, back). An authored player **body** always uses `FpsMove` (WASD translates the extras node; camera boom-follows). Free-fly `DesktopMove` is only the no-body inspector — never a third-person character.

| | First person | Third person |
|--|----------------|--------------|
| Position | root + `eye_offset` | `apply_follow_boom`: eye = (root + up·boom.y) − look·boom.z + right·boom.x |
| Look | yaw/pitch on camera | look-at root + boom.y (head height); pitch orbits |
| Player yaw | authored rotation kept | **body faces camera yaw** (rest rotation preserved, Y-only) |

Mouse still drives yaw/pitch. **`boom_offset` is asset meters** (same as `eye_offset`); load multiplies by `worldScale`. Human unscaled: `[0, 1.6, 3]`. Small assets (~0.05 m tall): `[0, 0.08, 0.15]`. Load applies Blender `ecs_components_settings` first, then **`ECS_Components_v1`** (hand-edited array wins). Re-export if you use the Blender component UI. Extras:

```json
{ "type": "player", "camera": "third_person", "boom_offset": [0, 1.6, 3],
  "forward": [0, 0, 1] }
```

`boom_offset` without `camera` also enables follow. Toggle later (V key).

**Order:** 3rd-person camera landed **before** crossfade (visible Walk hard-cut is OK). Then fade. Then graph.

### 9.4 Suggested implementation order

1. [x] `CameraRig` third-person boom + look-at; extras `player` + `camera` / `boom_offset`.  
2. [x] Crossfade in `AnimationSystem`; N uses fade (0.2 s).  
3. [x] Mask player-object TRS + skeleton `root` translation (Walk/Run in place).  
4. [x] `LocomotionAnim` + thresholds from `FpsMove.horizontal_speed` (accepts `locomation_anim`). Default: any WASD → walk; `run_speed` extras required to enter run.  
5. Polish hysteresis / jump clips later (`Jump_*` not required for v1).

### 9.5 Blender: copy clips between characters

Works on **any** scene with two armatures that share bone names (`root`, `hips`, `spine`, … — Mixamo prefixes like `mixamorig:Hips` match on the tail). Addon: `tools/blender/src/animation_transfer/` (install + README there).

1. Select **source**, then **destination** (destination **active**).
2. **Detect source → dest** (exactly two unique armatures in the selection, or in the scene).
3. Copy method **Duplicate Actions** (picks *how* — does not run). Leave **Scale location to dest size** on if they differ in size.
4. Click **Transfer Animations**. Optionally deletes the source hierarchy.
5. If a 1:1 copy is already on dest: **Scale dest clip locations** (skip idle/bind). Do not stack.
6. Export dest: glTF **Animation → NLA Tracks** and **Include → Custom Properties** so clip names match `locomotion_anim` extras.

**Example:** Kenney `_Bot` → `_Barbarian` on a combined chess scene that only exported `Running_A`. Same workflow as any other pair.

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
