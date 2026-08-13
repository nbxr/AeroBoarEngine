# Tech Context: AeroBoarEngine

## Core Tech Stack
- **Language**: C++ (strong preference for C-style structs and static functions)
- **Graphics API**: Vulkan 1.3+
- **VR Platform**: Meta Quest 3 (Adreno 740, TBDR architecture)
- **Memory Management**: VulkanMemoryAllocator (VMA)

## Key Libraries & Extensions
- **Vulkan Extensions**:
  - `VK_KHR_multiview` (mandatory for stereo on Android)
  - `VK_EXT_descriptor_indexing` (for bindless rendering)
- **Math**: GLM
- **Build System**: CMake
- **Shader Compilation**: `glslc` → SPIR-V (current desktop path)
- **GLTF**: tinygltf
- **Physics (runtime)**: Jolt Physics v5.3.0 (`physics::PhysicsWorld`, FetchContent target `Jolt`)
- **Physics (planned asset authoring)**: Khronos glTF extensions — see § Physics assets below
- **glTF extension matrix**: `docs/architecture/gltf-extensions.md` (supported vs backlog; AnimationPointerUVs, materials, physics)
- **VR chess product plan**: `docs/architecture/vr-chess-physics-plan.md`

### Physics runtime

- **Code:** `src/physics/PhysicsWorld.{h,cpp}`, `Engine::step_physics` / `spawn_scene_physics` / kill floor, plan in `docs/architecture/physics-plan.md`
- **Step:** fixed 1/60 with accumulator (max 4 substeps); gravity Y-up `-9.81`
- **Layers:** static `NON_MOVING` vs dynamic/kinematic `MOVING`
- **CCD:** dynamic bodies use Jolt `LinearCast` motion quality
- **Scene link:** optional `transform_index` per body; dynamic poses write local T+R each frame
- **Config:** `scenePhysics`, **`worldScale`** (root scale; mass × S³; chess `10`), **`physicsDebugDraw`** (+ **F3**), **`killFloor`** (`enabled` / `margin` or absolute `y`)
- **Debug draw:** Jolt wireframe → `gfx::DebugLinePass` (LINE_LIST overlay)
- **Kill floor:** destroy dynamic bodies below Y; hide linked mesh (scale 0)

### Physics assets (glTF Khronos extensions)

**Decision:** Model physics properties are **authored in glTF** using Khronos physics extensions (not a long-term proprietary physics format).

| Extension | Role |
|-----------|------|
| **`KHR_physics_rigid_bodies`** | Rigid bodies, motion, mass, materials, filters |
| **`KHR_implicit_shapes`** | Analytic colliders (box, sphere, capsule, …) |

- **Runtime:** `Engine::spawn_scene_physics` → Jolt (`PhysicsWorld`).
- **Status:** MVP (box/capsule/implicit + mesh convex hulls). Compound / triangle mesh later.
- **Authoring tip:** multi-material pieces → joined mesh + one RB; share mesh indices for GPU instancing; Blender “Render off” is **not** read by the engine.

### Shader tooling (future)
Today shaders are compiled with **`glslc`** via the CMake `compile_shaders` target. That is intentional while development is desktop-first.

**Planned change:** migrate to **`glslangValidator` / glslang** when cross-platform work begins in earnest (especially Meta Quest / Android and multi-target SPIR-V variants). Reasons to switch at that point:
- Stronger control over `--target-env` / SPIR-V version per platform
- Shared GLSL includes across many shaders
- Cleaner path for features like `gl_BaseInstance` and newer LocalSize modes
- One recipe for desktop vs mobile builds

Until then, prefer keeping `glslc` and avoiding a mid-feature toolchain swap.

### Depth buffer model (current vs planned reverse-Z)

**Today (desktop):** standard Vulkan Z.

- Clip depth **0 = near, 1 = far** (`GLM_FORCE_DEPTH_ZERO_TO_ONE` + normal `glm::perspective(near, far)`)
- Depth clear **1.0**, compare **`VK_COMPARE_OP_LESS`**
- Hi-Z is a **min-depth** pyramid; occlusion uses `z_near > hzb_sample + bias`

**Planned (official roadmap):** adopt **reverse-Z** when VR / multiview / Quest depth work begins (same window as HZB quality upgrades and stereo).

| Change | Reverse-Z target |
|--------|------------------|
| Projection | Infinite or large far plane; depth mapping so **near → 1, far → 0** (or equivalent swap of near/far in the Z row) |
| Clear | **0.0** |
| Compare | **`VK_COMPARE_OP_GREATER`** (or `GREATER_OR_EQUAL`) |
| Hi-Z | **Max-depth** pyramid (farthest occluder in the min-Z sense of reverse range); invert occlusion compare |
| Frustum / cull | Re-validate plane extraction and any NDC z assumptions in `core::Frustum` + compute cull |
| Resolve / MSAA depth | Keep working with the new clear/compare; re-check depth stencil resolve |

**Why schedule it with VR, not mid-desktop feature churn**

- Largest win is **depth precision at large far/near ratios** (world scale, HMD, outdoor scenes).
- Touches camera, pipeline, clear values, Hi-Z, and cull shaders in one coherent change.
- Stereo multiview benefits from one consistent depth convention from day one of OpenXR integration.

**Not planned as a drive-by desktop change** while the current standard-Z path is stable for inspection scenes. Desktop can migrate first as a prep PR for Quest if desired, but the **committed slot on the roadmap is Phase 3 VR / depth depth** (see `project-plan.md` and `current_state.md`).

## Coding Conventions
- Prefer **C-style structs** and static functions over class hierarchies (data-oriented design).
- **Naming**: `PascalCase` for structs/types, `snake_case` for functions and variables.
- All GPU memory is allocated via **VMA**.
- Prefer transient and lazily allocated memory for MSAA color and depth to keep data in GMEM when possible.
- Keep CPU-side data (transforms, instances, etc.) lightweight and cache-friendly.
- Namespaces: `core` (tiny universal utilities), `gfx` (all rendering/RHI/resources), `scene` (game object model + loading). See AGENTS.md for the current mapping.
- **Include Guards:** always use `#pragma once` instead of `#ifndef` and `#define`

### Occlusion / Hi-Z architecture (decision)

**VR constraint:** the HMD pose changes every frame. There is **no stable camera**. Any design that requires “hold still” or previous-view depth without reprojection **cannot** be the shipping occlusion path for Quest/OpenXR.

#### Current path (desktop, VR-ready shape)

**Same-frame occlusion** — **optional** (`occlusionCull`, default **false**). Adreno/Quest (`AERO_TARGET_ADRENO`) **forces off**. When on: RG min/max pyramid, conservative AABB query (fully on-screen + in front of near). See `docs/architecture/visibility-lod-plan.md`.

**Same-frame occlusion (depth prepass)** — hysteresis removed:

1. Frustum cull → candidates (indirect draws)  
2. **Depth-only prepass** at current pose (`depth_prepass` RP, 1x samples, vertex-only pipeline)  
3. Build Hi-Z pyramid from **this frame’s** prepass depth (`HzbPyramid::record_build`)  
4. Occlusion test + frustum → final visible set  
5. Full shading pass (main RP, MSAA; independent depth clear)

Depth prepass and HZB cull use the **same** `view_proj`. No camera-stability gate. Pyramid is double-buffered per frame-in-flight only to avoid concurrent submit stomps.

Pyramid quality: **RG32F** min+max downsample (`hzb_copy.comp` / `hzb_reduce.comp`). Occlusion uses **max** (G). Main-pass MSAA depth resolve is **not** the HZB source.

**Removed (dead end for VR):** previous-frame HZB + `should_use_occlusion` hysteresis.

**Secondary option still on the shelf: reprojected previous-frame HZB** (if prepass cost is too high on device).

**With reverse-Z / multiview:** redesign HZB compare (max vs min pyramid), per-eye or multiview-aware pyramid, GMEM-friendly subpass layout on Adreno — same phase as reverse-Z (see § Depth buffer model).

Code anchors: `Engine.Render.cpp`, `HzbPyramid`, `depth_prepass` / `init_depth_prepass*`, `cull_frustum.comp`, `hzb_copy.comp`.

### Input & Camera
The desktop `scene::Camera` is a quaternion-driven 6DOF camera intended for model/scene inspection during development.  
See the class documentation in `src/scene/Camera.h` for the current control scheme and public configuration options (`invert_pitch`, sensitivity, speed, etc.). Internal sign conventions for pitch/yaw/roll have been adjusted to match expected desktop behavior (normal pitch by default).

A `core::InputManager` provides the desktop input layer:
- High-precision mouse deltas via GLFW callbacks (sub-frame accumulation instead of per-frame polling).
- Per-frame processing: non-linear acceleration + EWMA smoothing.
- Keyboard/mouse-button state queries.
- Centralized cursor capture (`set_cursor_captured`) with automatic delta reset to prevent jumps on Escape.
- `Camera` now receives an `InputManager&` (decoupling raw GLFW details from the scene layer).

See `docs/architecture/desktop-inputs.md` for the full design (including rationale for singleton + user-pointer dispatch and config ownership split). The implementation is complete (see the companion implementation plan).

## Resource Lifetime Rules

**1. Vulkan Global (created once)**
- `VkInstance`, `VkPhysicalDevice`, `VkDevice`
- `VkPipelineLayout`
- `VkRenderPass`
- Descriptor set layouts and bindless descriptor sets
- Shader modules and pipelines
- `VkSampler` objects

**2. Per Pass**
- Framebuffers
- Fixed foveated density maps (when used)

**3. Per Subpass**
- Minimal state. Subpasses share render pass, pipeline layout, and bindless descriptor set.
- Only the pipeline object itself typically changes between subpasses.

**4. Per Frame in Flight** (`MAX_FRAMES_IN_FLIGHT = 2`)
- Command buffers
- Per-frame scene data buffers
- Indirect draw buffers and draw count buffers
- Synchronization objects (fences, semaphores)

**Rule of thumb**: Immutable or rarely-changing objects → global. Data written by the CPU and read by the GPU in the same frame → per-frame-in-flight.

## Global Bindless Descriptor Bindings
The single bindless descriptor set (allocated once, UPDATE_AFTER_BIND) uses these bindings. The texture array must be the highest binding number due to VARIABLE_DESCRIPTOR_COUNT requirements.

| Binding | Type                          | Count  | Purpose / Consumers                  | Notes |
|---------|-------------------------------|--------|--------------------------------------|-------|
| 0       | UNIFORM_BUFFER                | 1      | `FrameConstants` — camera xyz + exposure (w), lightCount, SH, IBL indices. std140-safe (vec4/uvec4 only). | `gfx::FrameConstants`; double-buffered per frame |
| 1       | STORAGE_BUFFER                | 1      | `DrawInstanceGPU[]` for instancing (model + material index) | Per-frame GPU cull output; multi-draw uses `firstInstance` = batch base (Vulkan `gl_InstanceIndex` already includes base) |
| 2       | STORAGE_BUFFER                | 1      | Materials (PBR + maps + multi-UV/transform); single packed SSBO, runtime array in shader. `gfx::Material` is **256 bytes** / align 16 (std430). | `gfx::MaterialManager` |
| 3       | STORAGE_BUFFER                | 1      | MeshPrimitiveSSBO metadata (v/i offsets) | `gfx::MeshManager` |
| 4       | STORAGE_BUFFER                | 1      | Vertex buffer (legacy SSBO view; primary path uses vertex attributes) | `gfx::MeshManager` |
| 5       | STORAGE_BUFFER                | 1      | Index buffer                         | `gfx::MeshManager` |
| 6       | STORAGE_BUFFER                | 1      | Lights (`GpuLight` array, std430, up to `MAX_LIGHTS`) | Scene KHR_lights_punctual or engine global fallback |
| 7       | COMBINED_IMAGE_SAMPLER        | 1      | Prefiltered specular env cubemap | `gfx::IblEnvironment` |
| 8       | COMBINED_IMAGE_SAMPLER        | 1      | BRDF integration LUT (2D) | `gfx::IblEnvironment` |
| 9       | COMBINED_IMAGE_SAMPLER        | 10000 (variable) | Bindless textures | `gfx::TextureManager`; **must** be last binding |

These are written once at scene load (after `update_buffers` + `toggle` + `bind_descriptor`). Shaders will access via the indices stored in the instance/material data.

## Compute Passes & Synchronization
- Compute work (culling; future GPU animation if any) runs before the graphics pass in the same command buffer when possible.
- Use pipeline barriers instead of extra semaphores between compute and graphics stages when feasible.
- Double buffering (`MAX_FRAMES_IN_FLIGHT = 2`) is the baseline for avoiding CPU/GPU hazards.
- Use deferred destruction for resources that may still be in use by the GPU.
- **CPU animation (planned Phase 1):** sample clips → `set_local_matrix` → `sync_scene_transforms` after the frame fence (same path as other transform mutations). Do not write cull item models mid-CB without FIF-safe buffers. Full plan: `docs/architecture/animation-plan.md`.

## Resource managers
- **CPU→GPU buffer managers** (`MaterialManager`, `SceneManager` instance SSBO, `MeshManager` vertex/index/meta): shared `gfx::DoubleBufferedBuffer` (upload/render pair, growth, memcpy, bind).
- **TextureManager** / **IblEnvironment**: image + sampler paths (not the buffer-pair helper).

## Build & Deployment
- Primary development target: desktop (Windows/Linux) for rapid iteration
- Target hardware: Meta Quest 3 (Android / aarch64)
- Shaders are compiled with `glslc` during the build (`--target-env=vulkan1.1` for graphics, `vulkan1.3` for compute); **glslang** planned for cross-platform — see above
- Optimization focus is on Adreno GMEM / tile-based rendering characteristics
