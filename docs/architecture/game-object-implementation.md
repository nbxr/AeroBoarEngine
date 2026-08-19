# Game Object Architecture

## Overview
Lightweight, cache-friendly data structures designed for a C++ Vulkan engine using GPU-driven rendering on Meta Quest 3. Focuses on efficient GLTF loading, multi-mesh / multi-material support (e.g. characters), and direct feeding into compute-based culling and indirect draw pipelines.

## Status
**Hierarchy + GPU cull path landed (see `docs/agents/current_state.md` for latest).**  

- `TransformManager` — local matrices + parent links + **dirty-flag** `propagate()` (load + runtime via `Engine::sync_scene_transforms`).
- `GameObject` — root transform + render-mesh range; one per glTF mesh node at load.
- `RenderMesh` — mesh/material/transform indices + **mesh-local** AABB; one per primitive.
- `SceneManager` — registry (`create_game_object`, `add_render_mesh`); framing and GPU cull read this model.
- Legacy `SceneInstance` is dual-written / refreshed after `propagate()` for compatibility.

**Animation:** See **`docs/architecture/animation-plan.md`**.

- **Phase 1 (landed):** node TRS clips — `AnimationSystem`, `gltf_node_to_transform`, TRS locals, auto-play loop.
- **Phase 2:** skinned meshes (JOINTS/WEIGHTS, IBM, joint palette, skin in shade **and** depth prepass).
- **Morph (CPU):** `MorphSystem` + animation `weights`. Advanced pointer/material extensions: see `gltf-extensions.md`.

**Physics:** Runtime **Jolt** foundation is in (`physics::PhysicsWorld`, body → `TransformManager` link). Collision / rigid-body **authoring** for production models will use Khronos glTF extensions (`KHR_physics_rigid_bodies`, `KHR_implicit_shapes`, …) mapped into engine components on `GameObject` / nodes — not a long-term proprietary physics asset format. See `docs/architecture/physics-plan.md` and `docs/agents/tech_context.md` § Physics assets.

## Core Principles

- Flat SOA-style layouts for high cache efficiency and low CPU overhead.
- One `RenderMesh` entry maps 1:1 to a draw / mesh-shader task.
- Global bindless storage for meshes, materials, and textures.
- Separate high-level ownership from low-level render data.
- Minimal CPU-side hierarchy; most work (culling, transform propagation, skinning) pushed to GPU.

## Key Structures

### GameObject

High-level entity container.  
Holds root transform, shared skinning data (if any), and game-relevant logic.  
One GameObject can own many `RenderMesh` entries (body + armor + weapon, etc.).

### RenderMesh

Thin render descriptor.  
Contains mesh reference, material reference, final transform index for culling, optional skin index, and local AABB.  
Supports both rigid and skinned pieces.

### Supporting Data

- **AABB**: Simple min/max bounds (mesh-local, expanded to world for culling).
- **Transform SOA**: Dense array of position, rotation, scale (or matrices).
- **Material**: Global SSBO with texture indices (albedo, normal, MR, etc.) and PBR factors.
- **MeshStorage**: Global GPU buffers holding all vertex data (future meshlets).

## Management Systems

### TransformManager

Manages a dense, contiguous array of transforms with hierarchy.

- Provides `transform_index` used by `GameObject` and `RenderMesh`.
- `set_local_matrix` / `set_parent` mark dirty; `propagate()` recomposes dirty nodes and cascades to children.
- `SceneManager::sync_transforms()` + `Engine::sync_scene_transforms()` run after the frame fence: CPU worlds, lights, per-frame GPU cull models.

### SceneManager (The Glue)

Orchestrates the lifecycle of all renderable data.

- **Registry**: Tracks all active `GameObject` and `RenderMesh` instances.
- **Buffer Orchestration**: Aggregates `RenderMesh` and `Transform` data into GPU-ready SSBOs for the compute culling pass.
- **Resource Lifecycle**: Coordinates with `GltfLoader` to populate `MeshStorage` and `MaterialManager`.

## GLTF Loading Flow

1. **Load**: `GltfLoader` parses file into `tinygltf::Model`.
2. **Register**: `SceneManager` iterates nodes, creating `GameObject` and `RenderMesh` entries.
3. **Populate**:
   - Meshes $\to$ `MeshStorage`.
   - Materials/Textures $\to$ Bindless Array + `MaterialManager`.
   - Transforms $\to$ `TransformManager` (SOA).
4. **Finalize**: `SceneManager` packs all `RenderMesh` descriptors into a single GPU-resident buffer for the culling pipeline.

## Handling Complex Objects

- Characters / multi-part models $\to$ single `GameObject` owning multiple `RenderMesh` entries.
- Shared skeleton via common `skin_index`.
- Rigid attachments (weapons, props) use bone-derived transforms copied into their `transform_index`.
- Multi-material meshes naturally split into separate `RenderMesh` entries.

## Rendering Pipeline Fit

- `RenderMesh` list feeds GPU culling compute (frustum / occlusion).
- Surviving items build indirect draw buffer or task shader input.
- **Today:** rigid `model * position` only. **Planned skinning:** joint palette in VS (shade + depth prepass) — see animation-plan Phase 2.
- Optimized for Quest 3: minimal bandwidth, few render passes, bindless resources.

## Animation data flow (target)

```text
glTF animations → clip/sampler tables (load)
       ↓
player time + sample T/R/S → set_local_matrix (dirty)
       ↓
sync_scene_transforms (after fence) → worlds / cull models / lights
       ↓
(frustum → depth prepass → HZB → cull → shade)  // existing
```

Skinned path adds joint palette rebuild after worlds, then VS skin (both prepass and shade).

## Potential Friction Points to Monitor

- Ensure that the "Buffer Orchestration" in the SceneManager only uploads changed transforms (dirty flagging) to prevent pipeline stalls.
- Ensure that when new GameObjects are spawned, they can update the bindless SSBO/Descriptor array without invalidating the bound descriptors mid-frame (usually handled by double-buffering descriptor sets or using `VK_EXT_descriptor_update_template`).

## Entity Component System (ECS)

**Near-term focus.** Full plan: **`docs/architecture/ecs-plan.md`**.

### Intent

- **Custom ECS permanently** (SoA; no EnTT/flecs). Package under **`src/ecs/`** — **not** `src/game/`.
- **Entity replaces `GameObject`** as the primary actor identity (phased migration; dual-write during transition).
- **Blender** authors components via glTF **`extras.ECS_Components_v1`**.
- **Physics** colliders: Khronos physics extensions long-term — not this JSON.

### Locked decisions (summary)

| Topic | Decision |
|-------|----------|
| Camera storage | Write into **`scene::Camera`**; render keeps reading it for now |
| glTF camera vs Player | **`player` entity owns the view**; glTF camera is not the active controller. No player → free-fly Player seeded from glTF camera or frame |
| Move input | **Player** path (`FpsMove` on a body; `DesktopMove` only if no body) |
| Editor keys (Escape, R, N, P, …) | **`EditorHotkeySystem`** — not on Player; Debug+Release for now |
| Multiple players | **First wins** for `active_player` |
| Triggers | **Jolt sensors** |
| Authoring key | **`ECS_Components_v1`** |
| Scripts | String **name** → **static self-registration + `REGISTER_SCRIPT` macro** → factory map → instance — see ecs-plan §11 |
| Event payloads | **Fixed union** on small POD `Event` for v1; frame arena only if we need fat/variable data later — ecs-plan §6.4 |

### GameObject vs Entity

| Today | Target |
|-------|--------|
| `GameObject` + `RenderMesh` arrays | **Entity** + transform link + renderable component rows |
| Identity for mesh nodes | **Entity** is identity; GameObject deleted after migration |

See ecs-plan **§4.6** for M0–M4 migration steps. Cull/draw keep dense tables; they gain an `entity` key rather than a separate GameObject id forever.

### Authoring sketch

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
| `player` | `PlayerTag` + `CameraRig` + `FpsMove` (optional `camera` / `boom_offset` / `eye_offset`) |
| `health` | `Health` |
| `script` | Register C++ class as `"player_controller"`; factory instantiates at entity create |

### Layers

| Layer | Role |
|-------|------|
| Managers | Own domain data (mesh, material, transform, physics, …) |
| `ecs::World` | Entities, components, events, timers |
| Systems | Frame logic (`DesktopMoveSystem`, **`EditorHotkeySystem`**, `ScriptSystem`, …) |
| `scene::Camera` | Pose/projection storage written by player move path |

### Input slice

1. `InputManager` — device only.  
2. `InputFrame` — snapshot + edges.  
3. **`DesktopMoveSystem`** — Player fly/look/speed → `scene::Camera`.  
4. **`EditorHotkeySystem`** — capture, frame, clip cycle, pose log (Debug+Release for now).  

Implement after **ecs-plan.md** sign-off.
