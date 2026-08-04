# VR Chess Physics Demo Plan

**Product fantasy:** Load **ABeautifulGame** (Khronos chess set), put **physics on the pieces and board**, enter **OpenXR / Quest**, **shrink the player** so you can run around on the board, and **knock pieces over** with hands / body collision.

**Status:** Design only. Physics **runtime foundation** (Jolt) exists; no KHR physics load, no OpenXR, no player-scale system yet.

**See also**
- `docs/architecture/physics-plan.md` — Jolt foundation + phase checklist  
- `docs/agents/tech_context.md` — physics assets, reverse-Z, occlusion/VR  
- `docs/project-plan.md` — VR / Quest roadmap  
- `docs/agents/current_state.md` — what is implemented today  

---

## 1. Goals

| Goal | Success criteria |
|------|------------------|
| Chess set is interactive | Pieces are dynamic rigid bodies; board/table static |
| Knock-over fun | Impulse / contact from player or debug push topples pieces |
| “I’m tiny on the board” | Player scale ~tabletop; locomotion feels like running on wood |
| Desktop first | Prove physics + interaction without headset |
| Quest later | Same scene under OpenXR multiview; controller grab/push |

**Non-goals (v1 demo):** full chess rules AI, networked multiplayer, perfect piece mesh colliders, audio.

---

## 2. Design decisions (agreed direction)

### 2.1 Authoring

- **Production path:** physics on models via **Khronos glTF extensions**  
  - `KHR_physics_rigid_bodies`  
  - `KHR_implicit_shapes`  
  (map → engine components → Jolt)
- **Do not** invent a long-term proprietary physics asset format for shipping content.
- **Pragmatic interim** (to get knock-over before KHR tooling is ready):  
  - Auto-box (or capsule) from mesh AABB per piece at load for ABeautifulGame  
  - Optional JSON sidecar for mass/friction overrides  
  - Replace with KHR when assets are authored

### 2.2 Scale (“shrink myself”)

| Approach | Recommendation |
|----------|----------------|
| **World in meters** | Keep board ~table scale (or one-time load scale of whole glTF) |
| **Player scale** | Scale avatar / locomotion so a step is a few mm–cm on the board |
| Avoid | Micro-world + micro-gravity (unstable sim) |

Physics stays in SI-ish units; **player scale** (and later XR reference space) carries the fantasy.

### 2.3 Motion types

| Object | Motion |
|--------|--------|
| Board / table / floor | **Static** |
| Chess pieces | **Dynamic** (or kinematic until first contact / grab) |
| Player | Capsule / character controller (later); or kinematic body for MVP |

---

## 3. Implementation phases

### Phase A — Desktop physics on ABeautifulGame (**next coding priority after session**)

1. Load `ABeautifulGame` as default/test scene.  
2. **Auto-collider pass:** each mesh GO → box (or convex) from local AABB → Jolt body linked to `transform_index`.  
3. Static board plane / thick box under set.  
4. Gravity + friction/restitution tuned so pieces topple and settle (sleep OK).  
5. Desktop debug interaction:  
   - Click / key **impulse** along camera forward  
   - Or raycast from mouse  
6. Disable free-fall `physicsDemo` cubes when scene provides bodies.

**Exit criteria:** Pieces fall/rest on board; can knock one over from desktop camera.

### Phase B — Authoring upgrade

1. Parse `KHR_implicit_shapes` + `KHR_physics_rigid_bodies` (as schemas stabilize).  
2. Prefer authored shapes/mass over AABB auto-box.  
3. Optional filter layers (piece vs board vs player).  
4. Fork or annotate ABeautifulGame with extensions if upstream has none.

### Phase C — Desktop “tiny player” prototype

1. Player capsule + simple kinematic controller (WASD already exists).  
2. Apply **player scale** factor (config: e.g. `playerScale: 0.03`).  
3. Collision vs board and pieces (push pieces on contact).  
4. Camera at eye height relative to scaled body.

### Phase D — OpenXR / Quest

1. OpenXR session + **multiview** stereo (project roadmap).  
2. Reverse-Z + per-eye / multiview Hi-Z as needed for stereo depth.  
3. Controller poses →  
   - near-field **grab** (kinematic attach) or  
   - **impulse** on contact  
4. Locomotion on board plane (smooth and/or teleport).  
5. Quest packaging / performance pass (TBDR, CPU budget for Jolt jobs).

---

## 4. Engine hooks already available

| Hook | Use |
|------|-----|
| `physics::PhysicsWorld` | Create static/dynamic boxes, fixed step, transform links |
| `Engine::step_physics` | Main loop after animations |
| `TransformManager` + `sync_scene_transforms` | Visual follow |
| `GameObject` / `RenderMesh` | One body per piece mesh node |
| `configuration.json` | Scene select, future `playerScale`, `physicsDemo` |

**Missing:** sphere/capsule/mesh shape API, raycast/query, contact events, character controller, OpenXR, KHR loader.

---

## 5. Frame order (target)

1. XR / desktop input  
2. Animation (if any)  
3. Player controller → kinematic body pose  
4. **Jolt step** (pieces + world)  
5. Physics → transform locals  
6. `sync_scene_transforms` → cull / draw  

---

## 6. Risks

| Risk | Mitigation |
|------|------------|
| Tiny player + float precision | Keep world ~1–2 m board, scale player not world to 1e-4 |
| Thin piece colliders jitter | Prefer slightly inflated boxes/capsules; CCD if needed |
| Grab vs simulation fight | Kinematic while grabbed; handoff velocity on release |
| Sample asset has no KHR physics | Phase A auto-box |
| Transparent / fancy materials on set | Unrelated; chess demo does not block on AnimationPointerUVs |

---

## 7. Suggested first PR after this session

1. `create_sphere` / better half-extents from AABB  
2. `Engine::spawn_physics_from_scene()` — walk render meshes → dynamic boxes (skip huge board by name/AABB heuristic or static flag)  
3. Static floor at scene min Y  
4. Desktop **F** key: ray impulse  
5. Doc: mark Phase A in progress in `current_state.md`

---

*Captured end of session 2026-08-03 from product discussion: ABeautifulGame + KHR physics direction + shrink-to-board VR knock-over.*
