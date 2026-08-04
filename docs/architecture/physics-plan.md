# Physics Implementation Plan

Canonical plan for rigid-body physics in AeroBoarEngine.

**Status:** **Foundation landed** — Jolt `PhysicsWorld`, Engine step/sync, floor+box demo.  
**Not yet:** glTF Khronos physics extensions, constraints, character controller, continuous collision polish.

**See also**
- `docs/agents/current_state.md` — status / next immediate
- `docs/agents/tech_context.md` — § Physics assets (KHR extensions)
- `docs/project-plan.md` — roadmap goals
- `docs/architecture/game-object-implementation.md` — hierarchy + transforms
- **`docs/architecture/vr-chess-physics-plan.md`** — ABeautifulGame + shrink-to-board VR knock-over product plan

---

## 1. Design decisions

| Topic | Choice |
|--------|--------|
| Runtime sim | **Jolt Physics** v5.3.0 (`FetchContent`, target `Jolt`) |
| Authoring (production) | Khronos glTF: `KHR_physics_rigid_bodies`, `KHR_implicit_shapes` (and related as ratified) |
| Coordinate / gravity | Y-up, gravity `(0, -9.81, 0)` — matches glTF / engine |
| Transform ownership | Body → optional `TransformManager` link; each frame physics writes **local** translation + rotation (scale preserved) |
| Step rate | Fixed **1/60 s**, accumulator, max 4 substeps/frame |
| Layers | 2 object layers: `NON_MOVING` (static) / `MOVING` (dynamic+kinematic); matching broadphase layers |

**Not inventing** a long-term proprietary physics asset format for production content; demo boxes are runtime-only until KHR load lands.

---

## 2. What landed (foundation)

### Code

| Piece | Location |
|--------|----------|
| World wrapper | `src/physics/PhysicsWorld.{h,cpp}` (Jolt pimpl) |
| Engine ownership | `gfx::Engine::physics` |
| Step + transform sync | `Engine::step_physics` → `PhysicsWorld::step` + `sync_to_transforms` |
| Main loop | `AeroBoar.cpp` after `update_animations`, before `render` |
| Draw rebuild | `Engine::rebuild_draw_batches` (shared with scene load) |
| Demo | `Engine::spawn_physics_demo` — static floor + 5 unit cubes (procedural mesh) |
| Config | `"physicsDemo": false` in `configuration.json` disables demo (default: on) |

### API (runtime)

```cpp
physics::BoxDesc desc{};
desc.half_extents = {0.5f, 0.5f, 0.5f};
desc.position = {0, 4, 0};
desc.motion = physics::MotionType::Dynamic;
desc.transform_index = transform_slot; // or set_transform_link later
auto h = engine.physics.create_box(desc);
// floor:
engine.physics.create_floor(half_xz, half_height, y_center);
```

After step, linked **dynamic** bodies push pose into `TransformManager` locals; `sync_scene_transforms` (inside `render`) propagates worlds + GPU cull models.

### Frame order

1. Input / camera  
2. `update_animations` (glTF clips)  
3. `step_physics` (Jolt + body → local TRS)  
4. `render` → fence wait → `sync_scene_transforms` → cull / draw  

Physics wins on dual-owned nodes if both animation and physics write the same transform (demo bodies are not animated).

---

## 3. Phases

### Phase 0 — Foundation (**done**)

- [x] FetchContent Jolt + link
- [x] Layers, PhysicsSystem, fixed step
- [x] Create static/dynamic box, floor helper
- [x] Body ↔ transform link + sync
- [x] Engine + main loop
- [x] Visible demo (procedural cubes)
- [x] Docs (`physics-plan.md`, current_state)

### Phase 1 — Shapes + scene wiring (**next toward chess demo**)

- [ ] Sphere / capsule / convex hull create APIs
- [ ] **Auto-box ABeautifulGame pieces** from mesh AABB → dynamic bodies (interim before KHR)
- [ ] Static board / floor under scene min Y
- [ ] Desktop ray / key impulse to knock pieces
- [ ] Static mesh collider path (Jolt `MeshShape` from authored mesh)
- [ ] Clear/re-init physics on scene reload without full process teardown
- [ ] Config-driven demo params; gate free-fall cubes when scene bodies exist  

Full product sequencing: **`vr-chess-physics-plan.md`**.

### Phase 2 — glTF Khronos physics

- [ ] Parse `KHR_implicit_shapes` (box/sphere/capsule/…)
- [ ] Parse `KHR_physics_rigid_bodies` (motion type, mass, filter, materials)
- [ ] Map node → body + shape → `PhysicsWorld` + transform link
- [ ] Sample / test assets with extensions (authored ABeautifulGame fork if needed)
- [ ] Disable or gate free-fall demo when KHR bodies present

### Phase 3 — Gameplay systems

- [ ] Collision filters / layers beyond static/dynamic
- [ ] Triggers / contact callbacks (engine-facing, not raw Jolt spam)
- [ ] Constraints / joints as KHR supports them
- [ ] Character controller (Quest locomotion later)
- [ ] Sleeping / activation policy tuned for mobile

### Phase 4 — Quest / mobile

- [ ] Thread budget: job system size vs XR frame
- [ ] Determinism / fixed step vs OpenXR predicted time (document policy)
- [ ] Avoid physics work on wrong queue; keep step on CPU sim thread if needed

---

## 4. Non-goals (for now)

- Soft bodies / cloth  
- Vehicle systems (Jolt has them; not wired)  
- Debug renderer integration (optional later)  
- Server-authoritative networking physics  

---

## 5. Testing checklist

- [x] Debug build links `Jolt.lib` and runs desktop loop  
- [ ] With `physicsDemo` true: orange boxes fall and rest on invisible floor at y≈0  
- [ ] With `physicsDemo` false: no extra boxes, no crash  
- [ ] Scene still renders after demo spawn (GPU cull rebuild)  
- [ ] Validation clean after long run with demo  

---

## 6. Open questions

- Should animation and physics share a priority policy (mask, or animation only for kinematic)?  
- KHR mass properties vs Jolt auto-inertia: follow extension when present.  
- Floor height for demos relative to glTF scene AABB (auto-place under min Y)?  
