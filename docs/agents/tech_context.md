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
