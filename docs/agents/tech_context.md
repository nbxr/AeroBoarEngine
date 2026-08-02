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
- **Shader Compilation**: `glslc` → SPIR-V
- **GLTF**: tinygltf

## Coding Conventions
- Prefer **C-style structs** and static functions over class hierarchies (data-oriented design).
- **Naming**: `PascalCase` for structs/types, `snake_case` for functions and variables.
- All GPU memory is allocated via **VMA**.
- Prefer transient and lazily allocated memory for MSAA color and depth to keep data in GMEM when possible.
- Keep CPU-side data (transforms, instances, etc.) lightweight and cache-friendly.
- Namespaces: `core` (tiny universal utilities), `gfx` (all rendering/RHI/resources), `scene` (game object model + loading). See AGENTS.md for the current mapping.
- **Include Guards:** always use `#pragma once` instead of `#ifndef` and `#define`

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
| 1       | STORAGE_BUFFER                | 1      | `DrawInstanceGPU[]` for instancing (model + material index) | Built at load; one range per mesh batch |
| 2       | STORAGE_BUFFER                | 1      | Materials (PBR + texture indices); single packed SSBO, runtime array in shader. `gfx::Material` is 80 bytes / align 16 (std430). | `gfx::MaterialManager` |
| 3       | STORAGE_BUFFER                | 1      | MeshPrimitiveSSBO metadata (v/i offsets) | `gfx::MeshManager` |
| 4       | STORAGE_BUFFER                | 1      | Vertex buffer (legacy SSBO view; primary path uses vertex attributes) | `gfx::MeshManager` |
| 5       | STORAGE_BUFFER                | 1      | Index buffer                         | `gfx::MeshManager` |
| 6       | STORAGE_BUFFER                | 1      | Lights (`GpuLight` array, std430, up to `MAX_LIGHTS`) | Scene KHR_lights_punctual or engine global fallback |
| 7       | COMBINED_IMAGE_SAMPLER        | 1      | Prefiltered specular env cubemap | `gfx::IblEnvironment` |
| 8       | COMBINED_IMAGE_SAMPLER        | 1      | BRDF integration LUT (2D) | `gfx::IblEnvironment` |
| 9       | COMBINED_IMAGE_SAMPLER        | 10000 (variable) | Bindless textures | `gfx::TextureManager`; **must** be last binding |

These are written once at scene load (after `update_buffers` + `toggle` + `bind_descriptor`). Shaders will access via the indices stored in the instance/material data.

## Compute Passes & Synchronization
- Compute work (culling, animation, etc.) runs before the graphics pass in the same command buffer when possible.
- Use pipeline barriers instead of extra semaphores between compute and graphics stages when feasible.
- Double buffering (`MAX_FRAMES_IN_FLIGHT = 2`) is the baseline for avoiding CPU/GPU hazards.
- Use deferred destruction for resources that may still be in use by the GPU.

## Build & Deployment
- Primary development target: Linux desktop (for rapid iteration)
- Target hardware: Meta Quest 3 (Android / aarch64)
- Shaders are compiled with `glslc` during the build
- Optimization focus is on Adreno GMEM / tile-based rendering characteristics
