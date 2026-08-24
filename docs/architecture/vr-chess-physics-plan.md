# VR Chess Physics Demo Plan

**Product fantasy:** Load **ABeautifulGame** (Khronos chess set), put **physics on the pieces and board**, enter **OpenXR / Quest**, **shrink the player** so you can run around on the board, and **knock pieces over** with hands / body collision.

**Status:** **Desktop Phase A largely working** — KHR rigid bodies on ABeautifulGame / ABeautifulGameGame, `worldScale: 10`, player knocks pieces, physics debug draw. **FpsMove + third-person boom landed** (WASD moves the authored body; `boom_offset` is asset meters × `worldScale`). Engine and Blender addons are **scene-agnostic** (extras + KHR, not chess/Barbarian name checks). Combined chess+player still needs Idle/Walk clips exported on that asset. **Not yet:** finished multi-part pawn authoring for all pieces, OpenXR / shrink-to-board VR.

**Next coding priority:** locomotion polish (clips / hysteresis / jump) or VR path — see `ecs-plan.md` / `animation-plan.md` §9 / `physics-plan.md`.

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
| **World in meters** | Author board ~table scale in glTF; use config **`worldScale`** (e.g. `10`) so sim sizes clear Jolt contact tolerances on thin boards |
| **Player scale** | Scale avatar / locomotion so a step is a few mm–cm on the board (relative to scaled world, or reintroduce “tiny player” after XR) |
| Avoid | Micro-world + micro-gravity (unstable sim); relying on paper-thin colliders at 1× without scale or thickened board |

Physics stays SI-ish under gravity 9.81; **`worldScale`** enlarges the loaded set for stable contacts. VR “I’m tiny on the board” is still mostly **player / reference-space scale**, not micro-gravity.

### 2.3 Motion types

| Object | Motion |
|--------|--------|
| Board / table / floor | **Static** |
| Chess pieces | **Dynamic** (or kinematic until first contact / grab) |
| Player | Capsule / character controller (later); or kinematic body for MVP |

---

## 3. Implementation phases

### Phase A — Desktop physics on ABeautifulGame (**mostly done**)

1. [x] Load ABeautifulGameScene as default/test scene.  
2. [x] KHR rigid bodies / hulls / board (not interim AABB-only).  
3. [x] Pieces rest / interact under **`worldScale: 10`** + CCD.  
4. [x] Player capsule contact knocks pieces (no impulse tool required for MVP).  
5. [x] Physics debug draw (F3).  
6. [ ] Finish asset: multi-part pawns → joined mesh + one RB; shared meshes for instancing.  

**Exit criteria (MVP met):** Pieces rest on board; player can knock pieces over on desktop.

### Phase B — Authoring polish (**in progress in asset**)

1. [x] Parse `KHR_implicit_shapes` + `KHR_physics_rigid_bodies` (engine MVP).  
2. [x] Shared multi-prim pawn meshes (body+top) for black/white + one RB each.  
3. [ ] Optional filter layers (piece vs board vs player).  
4. Keep physics on glTF; no engine hide-mesh workarounds.

### Phase C — Desktop player controller

1. [x] Player capsule + ECS Player.  
2. [x] **FPS controller** (`FpsMove`): grounded walk, mouse look, jump, crouch.  
3. [x] Collision vs board and pieces (push on contact).  
4. [ ] **Third-person** + skinned player + smooth animation transitions.  
5. [ ] Optional later: “tiny player” scale fantasy / VR reference space.

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
| Tiny features vs Jolt slop | Config **`worldScale`** (chess `10`); optional thicker board colliders |
| Thin piece / board tunneling | LinearCast CCD + scale; avoid paper-thin statics |
| Multi-mesh pieces split | One joined mesh + one RB; share mesh for instancing |
| Grab vs simulation fight | Kinematic while grabbed; handoff velocity on release |
| Free-fly camera for “player” | Replace with FPS grounded controller |

---

## 7. Next session

1. **Third-person controller** + animated player model  
2. Smooth animation transitions (crossfade / blend between clips)  
3. Optional: ray impulse; ECS Phase 4 events  

---

*Updated: FPS mover landed; pawns shared multi-prim meshes; next 3rd-person + animation.*
