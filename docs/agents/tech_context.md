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
- **Profiler (optional)**: Tracy v0.14.1 (`TracyClient`, FetchContent). CMake `-DAERO_TRACY=ON` sets `TRACY_ENABLE` + on-demand on **both** TracyClient and the engine (otherwise `ZoneScoped` is a no-op). Header `src/core/Profiler.h`. Instrumented: `FrameMark` per loop, `CpuScope` zones (`cpu.input` … `cpu.present`), load (`load.scene` / `load.meshes`), Vulkan GPU zones via `GpuTimestamps` (`gpu.shadow` …), plots `cull.vis` / `meshlet.drawn`. GUI is the GitHub release binary (port 8086), not built by this project.
- **HUD / overlay text**: `gfx::HudTextPass` — 8×8 atlas, alpha-blended quads. `HudSpace::Screen` (pixels, top-left) for desktop overlay; `HudSpace::View` (meters on a camera-space plane) for a later head-locked VR HUD. Drawn after WBOIT on the swapchain (1×, load color → present). Not bindless. `shaders/hud_text.{vert,frag}`.
- **Frame stats**: `core::FrameStats` (`src/core/FrameStats.h`) CPU RAII scopes (`CpuStage`) + `gfx::GpuTimestamps` query pool (`src/gfx/GpuTimestamps.{h,cpp}`, `GpuStage`). Overlay (F4) shows wall FPS, **busy vs gpu-wait**, EMA (α=0.1) + min/max, auto ns/µs/ms, **vis** (instances) and **ml** (meshlet cone/frustum: drawn/tested). CPU rows are the previous frame (`n-1`); GPU timestamps are one FIF slot old (`n-2`). Timestamps are written at compute / vertex / color-attachment (depth+HZB ends BOTTOM_OF_PIPE). Stored values are still milliseconds. Extend by adding a `GpuStage` and `gpu_times.scope(cmd, fi, stage)`. Unused GPU stages stay `--`. Instance/meshlet cull **lines** also go to `aero_boar.log` on change. Tracy (`AERO_TRACY`) records the same CPU/GPU stage names plus `FrameMark`.
- **Physics (planned asset authoring)**: Khronos glTF extensions — see § Physics assets below
- **glTF extension matrix**: `docs/architecture/gltf-extensions.md` (supported vs backlog; AnimationPointerUVs, materials, physics)
- **VR chess product plan**: `docs/architecture/vr-chess-physics-plan.md`

### Physics runtime

- **Code:** `src/physics/PhysicsWorld.{h,cpp}`, `Engine::step_physics` / `spawn_scene_physics` / kill floor, plan in `docs/architecture/physics-plan.md`
- **Step:** fixed 1/60 with accumulator (max 4 substeps); gravity Y-up `-9.81`
- **Layers:** static `NON_MOVING` vs dynamic/kinematic `MOVING`
- **CCD:** dynamic bodies use Jolt `LinearCast` motion quality
- **Scene link:** optional `transform_index` per body; dynamic poses write local T+R each frame
- **Config:** `scenePhysics`, **`worldScale`** (root scale; mass × S³; small assets e.g. chess `10`), **`physicsDebugDraw`** (+ **F3**), **`killFloor`** (`enabled` / `margin` or absolute `y`)
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

### Depth buffer model (reverse-Z — landed on desktop)

**Today:** reverse-Z, Vulkan [0, 1] range.

- Clip depth **1 = near, 0 = far** (`GLM_FORCE_DEPTH_ZERO_TO_ONE` + `perspective` then `clip.z' = clip.w - clip.z`)
- Depth clear **0.0**, compare **`VK_COMPARE_OP_GREATER`** (debug lines `GREATER_OR_EQUAL`)
- Constants: `gfx/Depth.h`
- Hi-Z stays **RG min/max**. Conservative occlusion uses **R (min)** as farthest/hole; cull iff `z_close < hzb.r - bias`. Sky/holes stay at 0 → no cull.
- Frustum extraction is unchanged: visible slab is still `0 <= ndc.z <= 1`.
- MSAA depth resolve prefers **MAX** (closest under reverse-Z). HZB source remains the 1× prepass.

**Why:** far-field precision at large far/near ratios (worldScale, outdoor, later HMD). Same convention for upcoming multiview.

**Still later (VR/Quest):** per-eye / multiview HZB, GMEM-friendly vis — `visibility-lod-plan.md`.

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

**Load-time meshoptimizer (landed):** indexed weld / vertex cache / overdraw / vertex fetch + meshlets (64/126) at glTF load (`gfx::MeshOptimizer`, FetchContent `meshoptimizer` v1.2). GPU meshlet cone+frustum after instance cull (`cull_meshlets.comp`). `"optimizeMeshes"` / `"meshletCull"` in configuration.json (default true). Morph extras participate in weld equality; skin/morph skip meshlet cone cull.

**Distance LOD (planned):** **CascadeBake** (`CascadeOven`) — meshoptimizer meshlets → octahedral impostors → static-only skybox bake; job-system background work. Classification from animation + KHR rigid-body motion. See `docs/architecture/cascadebake-plan.md`.

**Same-frame occlusion (depth prepass)** — hysteresis removed:

1. Frustum cull → candidates (indirect draws)  
2. **Depth-only prepass** at current pose (`depth_prepass` RP, 1x samples, vertex-only pipeline)  
3. Build Hi-Z pyramid from **this frame’s** prepass depth (`HzbPyramid::record_build`)  
4. Occlusion test + frustum → final visible set  
5. Full shading pass (main RP, MSAA; independent depth clear)

Depth prepass and HZB cull use the **same** `view_proj`. No camera-stability gate. Pyramid is double-buffered per frame-in-flight only to avoid concurrent submit stomps.

Pyramid quality: **RG32F** min+max downsample (`hzb_copy.comp` / `hzb_reduce.comp`). Occlusion uses **min (R)** — farthest surface / hole under reverse-Z. Query is `ceil(log2)` + 4-corner min. Main-pass MSAA depth resolve is **not** the HZB source.

**Removed (dead end for VR):** previous-frame HZB + `should_use_occlusion` hysteresis.

**Secondary option still on the shelf: reprojected previous-frame HZB** (if prepass cost is too high on device).

**Reverse-Z HZB compare is landed** (min = far/hole). Still later: per-eye / multiview pyramid, GMEM-friendly subpass on Adreno.

Code anchors: `Engine.Render.cpp`, `HzbPyramid`, `depth_prepass` / `init_depth_prepass*`, `cull_frustum.comp`, `hzb_copy.comp`.

### Input & Camera
The desktop `scene::Camera` is a quaternion-driven 6DOF camera intended for model/scene inspection during development.  
See the class documentation in `src/scene/Camera.h` for the current control scheme and public configuration options (`invert_pitch`, sensitivity, speed, etc.). Internal sign conventions for pitch/yaw/roll have been adjusted to match expected desktop behavior (normal pitch by default).

A `core::InputManager` provides the desktop input layer:
- High-precision mouse deltas via GLFW callbacks (sub-frame accumulation instead of per-frame polling).
- Per-frame processing: non-linear acceleration + EWMA smoothing.
- Keyboard/mouse-button state queries.
- Centralized cursor capture (`set_cursor_captured`) with automatic delta reset to prevent jumps on Escape.
- Local captured path: `GLFW_CURSOR_DISABLED` (infinite relative look). Must stay unchanged when the remote path is edited.
- Remote/HIDDEN path (RDP / xRDP): `GLFW_CURSOR_HIDDEN`; Windows look from `WM_INPUT` (relative or scaled absolute). No `ClipCursor`. Warp only at the desktop / 0–65535 rail, and only if `GetCursorPos` actually moved.
- **Accepted limit:** remote + trackpad look can still peg at that rail (`SetCursorPos` typically ignored). Parked until **camera cleanup**. See `docs/architecture/desktop-inputs.md` § Accepted look-rail limit.
- **GPU reset / RDP:** Windows TDR or DWM “composition off” shows up as `VK_ERROR_DEVICE_LOST` / `SURFACE_LOST`. `try_recover_gpu()` rebuilds the device and reloads the last scene (cap 3). Remote / composition-off prefers FIFO present (MAILBOX/IMMEDIATE fallbacks). Fence waits use a 1s timeout and skip the frame. After TDR the loader may omit `VK_KHR_win32_surface` (`windowing_extensions_not_present`) — instance create retries ~8s; if it never returns, reboot. See `desktop-inputs.md`.
- **GPU cull buffers (Adreno/UMA):** packed `worlds[]` (one mat4 per transform); 64-byte `GpuCullItem`; instance + indirect **GpuOnly** (unmapped) when WBOIT GPU-emits. Frame UBO: camera every frame, lights/SH only when dirty. No staging copies for HostWrite.
- **GPU skin palettes:** `skin_palette.comp` writes `inv(meshWorld)*jointWorld*IBM` from `worlds[]`. Palette SSBO is GpuOnly. VS unchanged. CPU palette path only if compute init fails. `worlds[]` re-uploads when `TransformManager::world_serial()` changes — FpsMove/physics may already have propagated (dirty flags clear) before `render()`.
- Consumers: `InputFrame` → `DesktopMoveSystem` / `FpsMoveSystem` (look+move) and `EditorHotkeySystem` (Escape, R, N, …). `Camera` is pose storage (ecs-plan option A), not the device layer.

See `docs/architecture/desktop-inputs.md` for the full design (singleton + user-pointer dispatch, capture reset, remote path, parked look-rail).

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
| 0       | UNIFORM_BUFFER                | 1      | `FrameConstants` — camera + exposure, lightCount, SH, IBL, **CSM VPs**. std140-safe (vec4/mat4). | `gfx::FrameConstants`; double-buffered |
| 1       | STORAGE_BUFFER                | 1      | `DrawInstanceGPU[]` for instancing (model + material index) | Per-frame GPU cull output; multi-draw uses `firstInstance` = batch base (Vulkan `gl_InstanceIndex` already includes base) |
| 2       | STORAGE_BUFFER                | 1      | Materials (PBR + maps + multi-UV/transform); single packed SSBO, runtime array in shader. `gfx::Material` is **256 bytes** / align 16 (std430). | `gfx::MaterialManager` |
| 3       | STORAGE_BUFFER                | 1      | MeshPrimitiveSSBO metadata (v/i offsets) | `gfx::MeshManager` |
| 4       | STORAGE_BUFFER                | 1      | Vertex buffer (legacy SSBO view; primary path uses vertex attributes) | `gfx::MeshManager` |
| 5       | STORAGE_BUFFER                | 1      | Index buffer                         | `gfx::MeshManager` |
| 6       | STORAGE_BUFFER                | 1      | Lights (`GpuLight` array, std430, up to `MAX_LIGHTS`) | Scene KHR_lights_punctual or engine global fallback |
| 7       | COMBINED_IMAGE_SAMPLER        | 1      | Prefiltered specular env cubemap | `gfx::IblEnvironment` |
| 8       | COMBINED_IMAGE_SAMPLER        | 1      | BRDF integration LUT (2D) | `gfx::IblEnvironment` |
| 9       | STORAGE_BUFFER                | 1      | Joint palettes (skin) | `SkinSystem` |
| 10      | COMBINED_IMAGE_SAMPLER        | 1      | Directional CSM (`sampler2DArrayShadow`) | `gfx::ShadowMap` 3 layers; silhouette extra planes; reverse-Z |
| 11      | COMBINED_IMAGE_SAMPLER        | 10000 (variable) | Bindless textures | `gfx::TextureManager`; **must** be last binding |

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
