# Entity Component System (ECS) Plan

Canonical plan for a **custom, permanent, data-oriented ECS** and the first slice: **Player + camera** driven by **injected input**, not hard-coded logic in `AeroBoar.cpp` / `Camera::update`.

**Status:** Phase 1–3 **landed** (InputFrame, World, Player, systems, `ECS_Components_v1` load, script registry + `log` example). Phase 4+ (events/timers/triggers) not started.

**See also**
- `docs/architecture/game-object-implementation.md` — legacy GameObject / RenderMesh; migration toward Entity
- `docs/architecture/desktop-inputs.md` — device input (`InputManager`)
- `docs/architecture/vr-chess-physics-plan.md` — later; Player/ECS first
- `docs/architecture/physics-plan.md` — Jolt
- `docs/architecture/cascadebake-plan.md` — CascadeOven reads animation + rigid-body motion
- `docs/architecture/animation-plan.md` — §9 third-person boom + locomotion anim graph
- `docs/agents/current_state.md` — priorities

---

## 1. Goals

### Near-term

| Goal | Success criteria |
|------|------------------|
| Custom ECS core (**permanent**) | Entity IDs + SoA stores + ordered systems; **no** EnTT/flecs |
| Input not hard-coded in main/camera | Device → `InputFrame` → player move handler + **app debug system** |
| Camera associated with Player | Active **Player** drives view via `scene::Camera` (storage option A) |
| ECS identity replaces GameObject | **Entity** is the scene actor; GameObject retired via migration (see §4.6) |
| Authoring | glTF `extras.ECS_Components_v1` (Blender) |
| Fit engine style | Flat SoA; coexists with GPU render path during migration |

### Later

| Goal | Notes |
|------|--------|
| Events / triggers / timers | §6–§8 |
| Scripts on entities | Registry + factory instantiation — §11 |
| Physics links, VR input | Same World |

### Non-goals (first milestones)

- Scripting language VM (Lua/etc.) — C++ scripts first via registry
- Big-bang rewrite of cull/draw in one PR
- Third-party ECS

---

## 2. Agreed decisions

| # | Decision |
|---|----------|
| **1. Camera storage** | **(A)** Gameplay writes **`scene::Camera`** (pose/projection). Render keeps reading it for now. **When this is cleaned up**, reopen the parked remote/trackpad look-rail (`desktop-inputs.md` § Accepted look-rail limit). |
| **2. glTF camera vs Player** | If a **`player`** entity exists → **it owns the view**; glTF camera is **not** the active controller (pose may seed once). If **no** player → spawn default free-fly Player; seed from glTF camera or AABB frame. |
| **3. Editor keys** | Not on Player. **`EditorHotkeySystem`**. **Debug and Release for now**; gate to debug-only later when practical. Player only gets locomotion/look. |
| **4. ECS library** | **Custom ECS permanently.** |
| **5. Authoring key** | **`ECS_Components_v1`** on glTF `extras` (versioned). |
| **6. No `game` package** | Code under **`src/ecs/`** (+ `core` / `scene` / optional `app`). |
| **7. GameObject** | **Entity replaces GameObject** as primary identity; phased migration §4.6. |
| **8. Multiple players** | **First wins** for `active_player` (first `PlayerTag` / first authored `player` in load order). |
| **9. Triggers** | **Jolt sensors** (primary path; not AABB-only stub design). |
| **10. Event payloads** | **Fixed union** on POD `Event` for v1 (§6.1). |
| **11. Scripts** | Static self-registration + **`REGISTER_SCRIPT`** macro (§11). |

---

## 3. Principles

1. **Entity** = opaque `uint32_t` (generation optional in v1).
2. **Components** = plain structs, dense SoA; sparse `entity → index`.
3. **Systems** = frame logic: `update(World&, …)`.
4. **Handlers** = optional **entity-local input policies** (e.g. desktop move). **Editor hotkeys** are a **system** (`EditorHotkeySystem`), not a player component.
5. **Render** stays GPU-friendly: dense `RenderMesh`-like rows, eventually owned/indexed by Entity.
6. **One transform story** — `TransformManager` (or future transform store) linked from Entity.
7. **Input is data** — device → `InputFrame` → systems/handlers.
8. **Events are data** — queue + dispatch (§6).
9. **Scripts** are C++ types registered by name; glTF only stores the **name** (§11).
10. **glTF extras = authoring** for gameplay components; **KHR physics** for colliders long-term.

---

## 4. Core architecture

### 4.1 Source layout (no `src/game`)

Avoid the word **game** in package names (ambiguous with GameObject / “gameplay” muddle).

| Path | Contents |
|------|----------|
| `src/core/` | `InputManager`, `InputFrame`, edges, log, config |
| `src/ecs/` | `World`, `Entity`, `ComponentStore`, event queue, timers, **systems**, **player move handler**, script registry |
| `src/scene/` | `Camera`, `TransformManager`, loaders, animation/skin/morph (until moved) |
| `src/gfx/` | Vulkan, managers for GPU resources, cull/draw |
| `src/app/` *(optional)* | `AeroBoar` shell, main loop orchestration only — **or** keep `AeroBoar.cpp` at `src/` |
| `src/physics/` | Jolt wrapper |

**Namespaces:** `ecs::`, `core::`, `scene::`, `gfx::`, `app::` (if used). Prefer `ecs::DesktopMoveHandler`, `ecs::EditorHotkeySystem` over a `game::` prefix.

### 4.2 World sketch

```cpp
namespace ecs {

using Entity = uint32_t;
constexpr Entity kInvalidEntity = ~0u;

struct World {
    // entity live flags + free list
    // ComponentStore<T>…
    // EventQueue, TimerStore
    Entity active_player = kInvalidEntity;
};

template<typename T>
class ComponentStore { /* dense SoA + entity maps */ };

} // namespace ecs
```

### 4.3 v1 components

| Component | Role |
|-----------|------|
| `TransformLink` | Index into transform store (`TransformManager` today) |
| `PlayerTag` | Controllable player; resolves `active_player` |
| `CameraRig` | FOV, near/far, sensitivity, movement_speed, invert_pitch, mode |
| `DesktopMove` *(tag or config)* | Entity receives desktop fly/look from `DesktopMoveHandler` / system |
| `Health` | `current`, `max` |
| `Script` | `name` + **runtime instance handle** after factory create (§11) |
| `Name` | Optional label |

**Render migration (replaces GameObject fields):**

| Component / table | Role |
|-------------------|------|
| `Renderable` or dense `RenderMesh` rows with `entity` | mesh, material, local AABB, skin index |
| (range of render rows per entity) | multi-primitive meshes |

**Later:** `RigidBodyLink`, `TriggerVolume`, …

### 4.4 Input: player move vs debug

**Problem today:** keys mixed in `Camera` + `AeroBoar.cpp`.

**Split:**

```text
InputManager (device)
    → InputFrame
         ├─→ Desktop move path (Player only)
         │      ecs::DesktopMoveSystem / DesktopMoveHandler
         │      reads entities with PlayerTag + DesktopMove
         │      writes scene::Camera (+ optional TransformLink)
         │
         └─→ Editor / tooling path (not on Player)
                ecs::EditorHotkeySystem
                Debug + Release for now (gate later)
                Escape capture, R / Shift+R frame, P log, N clip, …
```

| Concern | Where | Why |
|---------|--------|-----|
| WASD / mouse look / QE / **Y·T** speed | **Player** (`DesktopMove` + system) | Character / free-fly **intent** |
| Escape (capture), R, Shift+R, P, N, F3 (physics debug), F4 (stats HUD), … | **`EditorHotkeySystem`** | **Tooling / shell** — not “what the avatar does.” Runs in Debug **and** Release for now; later we may compile out or gate to debug-only |
| Cursor capture state | `InputManager` API called **from** EditorHotkeySystem | Device ownership stays core |

**Y/T:** treated as **move tuning** on the free-fly / player rig (with DesktopMove), not editor-only — unless we later split “editor camera speed” vs “player speed.”

**Why not editor keys on Player:** they are about **inspecting the world and app**, not embodying the player. Attaching them to Player couples tooling to whichever entity is active and is wrong for multiplayer / AI puppets / headless.

### 4.5 Camera selection after load

```text
if any Entity with PlayerTag:
    active_player = FIRST such entity (load / spawn order — "first wins")
    FpsMove drives the body + scene::Camera (third-person boom when authored)
    glTF camera node: NOT active controller
    optional one-shot: seed pose from glTF camera if player has no authored pose
else:
    spawn default free-fly Player (PlayerTag + CameraRig + DesktopMove)
    seed from glTF camera if present, else AABB frame
EditorHotkeySystem: runs in desktop builds (Debug + Release for now)
```

### 4.6 Entity replaces GameObject (migration)

You are **not** misunderstanding: long-term **Entity is the actor**. The current `scene::GameObject` struct is a pre-ECS bag:

```text
GameObject today:  root_transform, skin, first_render_mesh, count, flags, gltf_node
```

**Target end state**

```text
Entity
  + TransformLink
  + zero or more Renderable rows (mesh/material/aabb/skin)
  + optional PlayerTag, Health, Script, …
```

**Migration (do not big-bang)**

| Phase | What happens |
|-------|----------------|
| **M0 (today)** | `GameObject` + `RenderMesh` arrays drive GPU cull |
| **M1** | Introduce `ecs::World`; Player may exist with no GameObject |
| **M2** | Each loaded mesh node creates an **Entity**; keep dual-writing GameObject/RenderMesh for cull |
| **M3** | Cull/draw read render tables keyed by Entity (or densify RenderMesh with `entity` field); **delete GameObject** |
| **M4** | Optional: rename docs/files away from “game object” language |

Until M3, docs may say “GameObject (legacy render bag)” vs “Entity (ECS identity).”

`SceneManager` remains a **resource/orchestration manager** for load + buffers; it should not stay the long-term home of “all actors.”

---

## 5. Managers vs systems vs handlers

| Kind | Responsibility | Examples |
|------|----------------|----------|
| **Device / core service** | OS/API | `InputManager`, Vulkan device |
| **Domain manager** | Owns dense resources for one domain | `MeshManager`, `MaterialManager`, `TransformManager`, `PhysicsWorld`, `SceneManager` (during migration) |
| **ECS World** | Entities, components, events, timers | `ecs::World` |
| **System** | Frame logic over components / queues | `DesktopMoveSystem`, **`EditorHotkeySystem`**, `TimerSystem`, `EventDispatchSystem`, `ScriptSystem` |
| **Handler** | Optional helper used by a system for entity policy | move math extracted as `DesktopMoveHandler` functions |

**Rules**

- Managers = **data owners**.
- Systems = **frame logic**.
- Do **not** put debug hotkeys on Player or on `InputManager`.
- Prefer **no** new “GameplayManager” god object.

---

## 6. Events

Double-buffered queue: systems **push** events; `EventDispatchSystem` **drains** after physics/triggers/timers.

```text
EventHeader { type, source entity, target entity, time }
+ payload (see §6.1)
```

**v1 listen style:** systems that know how to react (SoA-friendly), plus **scripts** via `IScript::on_event` (§11).

Examples: `TriggerEnter/Exit`, `Damage`, `Died`, `TimerElapsed`, `Interact`.

### 6.1 Event payload: fixed union vs frame arena

Every event has a **header** (what happened, who/when). Many also need **extra fields** (damage amount, which volume, timer handle). That extra data is the **payload**. Two ways to store it:

#### Fixed union (**v1 default — locked**)

One `Event` struct, fixed size, with a C++ `union` (or `std::variant`) of all known payload shapes:

```cpp
struct Event {
    EventType type;
    Entity source, target;
    float time;
    union {
        struct { Entity volume; Entity other; } trigger_enter;
        struct { float amount; Entity instigator; } damage;
        struct { uint32_t timer_index; uint32_t timer_gen; } timer;
    } payload;
};
// use: switch (e.type) { case Damage: e.payload.damage.amount; ... }
```

| Pros | Cons |
|------|------|
| Simple POD queue, no allocator | Every slot is as large as the **largest** payload arm |
| No pointer lifetime bugs | Awkward for strings / big lists |
| Easy to debug | New fat types force union growth |

**Good for us:** triggers, damage, timers — a few floats and entity ids.

#### Frame arena (alternative, later if needed)

Header stays small; payload bytes live in a **per-frame bump allocator** wiped each frame:

```cpp
struct Event {
    EventType type;
    Entity source, target;
    float time;
    uint32_t payload_offset;
    uint16_t payload_size;
};
// push: auto* p = arena.alloc<DamagePayload>(); ...
// read: only valid until arena.reset() next frame
```

| Pros | Cons |
|------|------|
| Variable / large payloads | Easy use-after-free if you keep a pointer |
| Doesn’t bloat every event | Harder to reason about |

**Good when:** multi-contact lists, debug strings, bulk data “this frame only.”

#### Hybrid (optional later)

Fixed union for 95% of events; rare “fat” events use arena + offset.

**Decision:** start with **fixed union**. Revisit arena only if a real payload doesn’t fit cleanly.

---

## 7. Triggers

Volumes that detect overlap → enter/exit/stay → events. **Not** the same as solid physics colliders (no blocking response required).

**Chosen approach: Jolt sensors** (sensor bodies / no-collision-response shapes), not a separate primary AABB-only trigger path.

```text
Jolt sensor contact callbacks (or query)
  → push TriggerEnter / TriggerExit to EventQueue
  → EventDispatchSystem / scripts
```

Authoring long-term: KHR physics + filter layers, or ECS component that creates a sensor body at load. Align layers with physics plan (`TRIGGER` / `PLAYER` / etc. when added).

---

## 8. Timers

`TimerStore` + `TimerSystem`: one-shot/repeat, cancellable `(index, generation)` handles, fire `TimerElapsed` (or script notify). No scattered `static float t`.

---

## 9. Frame order (target)

```text
1. glfwPollEvents
2. InputManager::update
3. build InputFrame
4. EditorHotkeySystem(frame)            // app/editor — not on Player; Debug+Release for now
5. FpsMove if player has a body; else DesktopMove  // never 6DOF-fly a follow-cam body
6. LocomotionAnimSystem                 // speed → Idle/Walk/Run crossfade
7. update_animations                    // player-root channels masked
8. ScriptSystem::on_update(world, dt)
9. step_physics  (+ Jolt sensor contacts → Trigger* events when implemented)
10. TimerSystem → EventQueue
11. EventDispatchSystem (+ script on_event)
12. render (scene::Camera already updated)
```

---

## 10. glTF authoring (`ECS_Components_v1`)

```json
"extras": {
  "ECS_Components_v1": [
    { "type": "player" },
    { "type": "health", "current": 100, "max": 150 },
    { "type": "script", "name": "player_controller" }
  ]
}
```

| type | Runtime |
|------|---------|
| `player` | `PlayerTag` + `CameraRig` + `FpsMove` (not free-fly). `"camera": "third_person"` / `boom_offset` → follow boom |
| `health` | `Health` |
| `script` | See **§11** — name → factory → instance |

**Player + camera (authoring)**

- Put `{ "type": "player" }` on the **feet / body root** node (capsule origin on the ground).
- Camera = **eye**; body stays **upright** (yaw only — pitch/roll do not flip the capsule).
- `eye_world = root + yaw_only(R) * eye_offset`
- Optional on the player entry:
  - `"eye_height": 1.6` → `(0, 1.6, 0)` local Y. Small assets: `0.08`.
  - `"eye_offset": [0, 1.6, 0]` → full local offset (X/Z are side/forward in body space)
  - `"camera": "third_person"` → follow boom (default is first-person)
  - `"boom_offset": [right, up, back]` → **asset meters**, multiplied by `worldScale` at load (same as `eye_offset`). Implies third-person. Default `[0, 1.6, 3]`. Small assets (~0.05 m): `[0, 0.08, 0.15]`. **Z is distance behind the WASD heading** (sign ignored). Blender `ecs_components_settings` is applied first; **`ECS_Components_v1` wins** if both are present (re-export so the UI and the array stay in sync).
  - `"forward": [x, y, z]` → mesh facing in the node’s space. Engine WASD/camera forward is **(0,0,-1)**. Blender/glTF characters usually face **+Z** → `"forward": [0, 0, 1]` (same as `"yaw_offset": 180`).
  - `"yaw_offset": 180` → extra body yaw in degrees (added to `forward` if both are set).
- `{ "type": "locomotion_anim", "idle": "Idle", "walk": "Walk", "run": "Run", "fade": 0.2 }`
  - Mixamo-style: `"idle": "T-Pose", "walk": "Walking_A", "run": "Running_A"`.
  - Typo `locomation_anim` is accepted.
  - Optional `walk_speed` / `run_speed` are **thresholds** (m/s). Omit `run_speed` to stay on walk for any WASD.
- Default `eye_offset` `(0, 1.6, 0)` and boom `(0, 1.6, 3)` (human-scale asset meters). Small assets override via extras (Blender **small assets** presets).
- `eye_offset` and `boom_offset` are asset meters; load scales both by `worldScale`.
- Blender: origin at feet, **+Y up**, apply rotation; avoid 180° X export quirks if the mesh looks inverted in a glTF viewer.
- Do **not** key the player extras node in clips (gameplay owns that TRS). Bone `root` translation is masked so Walk/Run stay in place. Do **not** rewrite hips/feet after sample — see `animation-plan.md` §9.1.

Unknown types: log + skip.

---

## 11. Scripts: how a class gets instantiated

glTF only stores a **string name**. It does **not** load C++ from disk. Instantiation uses a **name → factory map** filled by **static self-registration** (Option B) — not reflection, not a central switch.

### 11.1 Chosen pattern: static registrar + macro (locked)

```cpp
// Interface
struct IScript {
  virtual ~IScript() = default;
  virtual void on_create(ecs::World&, ecs::Entity) {}
  virtual void on_update(ecs::World&, ecs::Entity, float dt) {}
  virtual void on_event(ecs::World&, ecs::Entity, const Event&) {}
  virtual void on_destroy(ecs::World&, ecs::Entity) {}
};

using ScriptFactory = std::unique_ptr<IScript>(*)();

// Registry: Meyers singleton (safe for static-init order)
// create(name) → map lookup → factory()

struct ScriptRegistrar {
  ScriptRegistrar(const char* name, ScriptFactory f) {
    ScriptRegistry::instance().add(name, f);
  }
};

// In each script .cpp (linked into the executable):
#define REGISTER_SCRIPT(NameStr, ClassType)                         \
  static ScriptRegistrar reg_##ClassType(NameStr,                   \
      []() -> std::unique_ptr<IScript> {                            \
        return std::make_unique<ClassType>();                       \
      })

// PlayerControllerScript.cpp
class PlayerControllerScript : public IScript { /* … */ };
REGISTER_SCRIPT("player_controller", PlayerControllerScript);
```

**Load path**

1. Blender writes `"script": { "name": "player_controller" }`.  
2. Entity create → `ScriptRegistry::create("player_controller")`.  
3. Unknown name → log, no script.  
4. Instance stored in **ScriptInstanceStore** (handle on POD component).  
5. `on_create` → each frame `on_update` → `on_destroy` on entity free.

**Caveats (document for implementers)**

- Registrar runs before `main`; registry must be function-local static.  
- Script `.cpp` must be in the **executable** source list (static libs can drop unreferenced TUs).  
- Still not reflection: only registered, compiled-in types.

### 11.2 Ownership

| Piece | Owner |
|-------|--------|
| `name → factory` | Process-lifetime `ScriptRegistry` |
| Instance | `ScriptInstanceStore` (polymorphic), handle on entity component |
| POD `Script` component | `{ name or type_id, instance_handle }` |

### 11.3 Later options (out of scope now)

- Hot-reload DLL that also calls `add`  
- Lua/Wren with the same `name` key  
- Multiple scripts per entity  

### 11.4 Relation to events / timers

Scripts implement `on_event`; dispatch delivers `TriggerEnter`, `TimerElapsed`, etc.

---

## 12. Implementation phases

### Phase 0 — Docs

- [x] Core plan + decisions  
- [x] No `game/` naming; Entity vs GameObject; debug system  
- [x] Scripts: static self-registration + `REGISTER_SCRIPT` macro (§11)  
- [x] Event payloads: fixed union default (§6.1)  
- [ ] User review / sign-off  

### Phase 1 — Input injection

1. [x] `InputFrame` + edges (`core/InputFrame.h`)  
2. [x] `DesktopMoveSystem` (player fly/look + Y/T speed)  
3. [x] `EditorHotkeySystem` (Escape, R, Shift+R, P, N, F3, F4; Debug+Release)  
4. [x] Strip `Camera` / `AeroBoar` key logic → `Camera::apply_desktop_input`  
5. [x] Minimal World (with Phase 2)  

### Phase 2 — ECS World + Player

1. [x] World + ComponentStore  
2. [x] PlayerTag, CameraRig, DesktopMove, Health, Script, Name  
3. [x] `active_player` first-wins + default free-fly spawn when no authored player  


### Phase 3 — glTF extras + script registry

1. [x] Parse `ECS_Components_v1` (`ecs/GltfEcsLoader`)  
2. [x] `ScriptRegistry` + `REGISTER_SCRIPT` + instance store + `ScriptSystem`  
3. [x] Dual-write Entity per mesh GameObject; extras on any node  
4. [x] Example script `"log"`; default free-fly player if no authored `player`

### Phase 4 — Events + timers  

### Phase 5 — Triggers via **Jolt sensors**  

### Phase 6 — Retire GameObject; render by Entity  

### Phase 7+ — Physics components, VR / tabletop demo  

---

## 13. Open items

| # | Item | Resolution |
|---|------|------------|
| 1 | Multiple `player` entities | **First wins** (load/spawn order) |
| 2 | Event payload storage | **Fixed union** (§6.1); arena later if needed |
| 3 | Triggers implementation | **Jolt sensors** (not AABB-primary) |
| 4 | Editor hotkeys build config | **Keep in Debug and Release for now**; make debug-only later when practical |
| 5 | System name | **`EditorHotkeySystem`** |
| 6 | Remote/trackpad look rail | **Accepted** for now (`desktop-inputs.md`). Reopen when camera storage / `scene::Camera` is cleaned up — not as a standalone input pass. |

**Phase 0 decisions 1–5 are closed.** Item 6 (look-rail) is parked until camera cleanup.

---

## 14. Success metrics

- [x] No gameplay/editor key branching in `AeroBoar.cpp` or `Camera::update`  
- [x] Player owns view; DesktopMove on player path only  
- [x] Editor keys via **EditorHotkeySystem** (not on Player); Debug+Release until gated later  
- [x] `active_player` = first `PlayerTag`  
- [ ] Triggers via Jolt sensors when Phase 5 lands  
- [x] No `src/game` package  
- [x] Documented path: Entity replaces GameObject  
- [x] Script `name` → `REGISTER_SCRIPT` / registry → `on_create` instance  
- [ ] Event queue uses fixed-size POD events (union payloads) for v1  
- [ ] Triggers use Jolt sensors  
- [x] active_player = first PlayerTag  
- [x] Custom ECS only  


---

*Open items 1–5 resolved. Item 6 parked (look-rail → camera cleanup). Scripts: Option B + macro. Events: fixed-union payloads.*
