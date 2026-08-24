# Physics Implementation Plan

Canonical plan for rigid-body physics in AeroBoarEngine.

**Status:** **Foundation + KHR load MVP** — Jolt `PhysicsWorld`, step/sync, demo boxes, `spawn_scene_physics` (`KHR_physics_rigid_bodies` / `KHR_implicit_shapes`), LinearCast CCD, **`worldScale`**, wireframe **debug draw**. Desktop knock-over verified on ABeautifulGameScene / ABeautifulGameGame (`worldScale: 10`) as the current tabletop test. Engine paths are scene-agnostic.  
**Not yet:** compound colliders, triangle mesh colliders, joints, rich filters, FPS/character controller, raycast/impulse API.

**See also**
- `docs/agents/current_state.md` — status / next immediate
- `docs/agents/tech_context.md` — § Physics assets (KHR extensions)
- `docs/project-plan.md` — roadmap goals
- `docs/architecture/game-object-implementation.md` — hierarchy + transforms
- **`docs/architecture/vr-chess-physics-plan.md`** — ABeautifulGame + shrink-to-board VR knock-over product plan
- `docs/architecture/cascadebake-plan.md` — static vs kinematic/dynamic eligibility for skybox bake

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
| **Scene / tabletop scale** | Config **`worldScale`** (default `1`). Uniform load-time scale of **root** transforms (mesh + physics). Mass × S³. Use for small assets so feature sizes sit above Jolt defaults like ~2 cm penetration slop without rewriting every collider. Gravity stays 9.81 in scaled meters. Current chess test uses `10`. Legacy alias: `debugWorldScale`. |
| Dynamic CCD | Dynamic bodies use Jolt **`EMotionQuality::LinearCast`** to reduce tunneling through thin statics |
| **Debug draw** | Config **`physicsDebugDraw`** + **F3** toggle. Jolt wireframe shapes via `DebugRendererSimple` → `gfx::DebugLinePass` (LINE_LIST overlay). Colors by motion type (static / kinematic / dynamic). |

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
| Config | `scenePhysics`, **`worldScale`**, **`physicsDebugDraw`**, **`killFloor`** in `configuration.json` |
| Debug draw | `gfx::DebugLinePass` + Jolt `DebugRendererSimple`; **F3** toggles |
| Scene KHR spawn | `Engine::spawn_scene_physics` (players → kinematic) |
| Kill floor | World policy (not ECS): destroy **dynamic** bodies below Y; hide mesh via scale 0. Auto from scene AABB − margin, or explicit `y` |

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

1. InputFrame + editor hotkeys + DesktopMove (player)  
2. `update_animations` (glTF clips)  
3. `step_physics` (kinematic from transforms → Jolt step → dynamics → local TRS)  
4. `render` → fence wait → `sync_scene_transforms` → cull / shade → optional physics debug lines  

Physics wins on dual-owned nodes if both animation and physics write the same transform (demo bodies are not animated).

### Kill floor (world policy)

Prefer **one scene/world setting**, not a component on every piece:

```json
"killFloor": { "enabled": true, "margin": 2.0 }
// or absolute sim Y (after worldScale):
"killFloor": { "enabled": true, "y": -5.0 }
```

- Default when `scenePhysics` loads: **enabled**, Y = framing center.y − radius − **margin** (sim meters).  
- Only **dynamic** bodies. Static colliders + **kinematic** player are ignored.  
- On kill: remove Jolt body; set linked transform **scale = 0** (hides mesh; no full GO GC yet).  
- ECS scene-level component later is optional; config is enough for tabletop.

### Multi-part colliders (authoring — model, not engine)

When a prop uses separate materials/meshes (body + glass top, etc.):

1. Prefer **one render mesh** with **multiple primitives/materials** (joined in Blender) **and** one dynamic rigid body whose convex hull uses that mesh.
   - Select the extra parts, then the body (body **active**) → **Ctrl+J**. Keeps material slots → multiple glTF primitives. Do **not** merge slots, Boolean-union, or bake to one material (that kills glass/transmission).
   - Physics only on that joined object. Copy the KHR rigid body from a working sibling if needed.
   - Same mesh across instances: **Ctrl+L → Link Object Data**. Delete leftover part objects.
   - Color/team variants stay separate meshes. Do **not** apply Subdivision on export (dense hulls make Jolt’s convex error check fail; the engine support-samples, but a 100k+ subdivided mesh is wasted).
2. Or: one RB root with **no extra draw mesh** and hull geometry that covers the whole prop; visuals as children.  
3. Blender “disable Render” is **not** honored by the engine — if a collider object has `node.mesh` in the glTF, it draws.  
4. **Share one glTF mesh** across identical instances so GPU instancing batches them (import does **not** content-hash-dedupe meshes).  
5. Do **not** put separate dynamic bodies on body vs top (they separate).

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

### Phase 1 — Shapes + scene wiring (**done for KHR MVP**)

- [x] Capsule / convex hull (+ box) create APIs
- [x] Dynamic **LinearCast** CCD; scaled box/hull convex radius for small shapes
- [x] Dense convex hulls **support-sampled** (not AABB corners — those drew as boxes). Skinned player hulls subtract baked armature Y so they sit on the body.
- [x] Kinematic player wakes sleeping dynamics in a padded AABB (knock-over after settle)
- [x] **`worldScale`** for tabletop / thin-collider scenes (collider points use **world** scale so skinned children under a scaled player root match the mesh)
- [x] Physics wireframe debug draw
- [x] Kill floor (config / auto scene bounds)
- [x] Removed free-fall `physicsDemo` cubes path
- [ ] Desktop ray / key impulse (optional; capsule contact already knocks pieces)
- [ ] Static mesh collider path (Jolt `MeshShape`)
- [ ] Clear/re-init physics on scene reload without full process teardown  

Full product sequencing: **`vr-chess-physics-plan.md`**.

### Phase 2 — glTF Khronos physics (**MVP landed**)

- [x] Parse `KHR_implicit_shapes` (box / capsule; sphere≈box MVP)
- [x] Parse `KHR_physics_rigid_bodies` (motion, mass, materials; filters basic)
- [x] Map node → body + shape → `PhysicsWorld` + transform link
- [x] ABeautifulGameScene KHR load + `worldScale`
- [ ] Compound colliders; non-hull mesh colliders; full filter systems

### Phase 3 — Gameplay systems

- [x] **FPS / grounded player controller** (`ecs::FpsMove`). Kinematic hulls are bind-pose on mesh nodes — they do **not** follow skinned feet. Ground is a static raycast on the extras empty, not Jolt depenetration (kinematics are not pushed out of the floor). Foot clip vs floor is authoring / future IK — `animation-plan.md` §9.1.
- [x] Third-person boom + `LocomotionAnim` Idle/Walk/Run (clips play as authored)
- [ ] Collision filters / layers beyond static/dynamic
- [ ] Triggers / contact callbacks (engine-facing)
- [ ] Constraints / joints as KHR supports them
- [ ] Jolt character controller (optional later vs custom FPS)
- [ ] Sleeping / activation policy tuned for mobile

### Phase 4 — Quest / mobile

- [ ] Thread budget: job system size vs XR frame
- [ ] Determinism / fixed step vs OpenXR predicted time (document policy)
- [ ] Avoid physics work on wrong queue; keep step on CPU sim thread if needed

---

## 4. Non-goals (for now)

- Soft bodies / cloth  
- Vehicle systems (Jolt has them; not wired)  
- Full solid-triangle debug mesh renderer (wire lines are enough)  
- Server-authoritative networking physics  
- Engine-side “hide render mesh” for Blender hide_render (fix in the asset)  

---

## 5. Testing checklist

- [x] Debug build links `Jolt.lib` and runs desktop loop  
- [x] KHR scene (`scenePhysics` + ABeautifulGameScene): bodies spawn; dynamics interact under `worldScale: 10`  
- [x] F3 / `physicsDebugDraw`: wire colliders visible  
- [x] Kill floor removes fallen dynamics  
- [x] FPS / third-person: walk, jump/crouch, keep knock-over  
- [ ] Validation clean after long run with physics + debug draw  

---

## 6. Open questions

- Animation vs physics write priority on shared nodes (mask / kinematic-only anim)?  
- KHR mass properties vs Jolt auto-inertia when both present.  
- Whether FPS uses pure kinematic capsule + custom ground logic vs Jolt `CharacterVirtual`.  
