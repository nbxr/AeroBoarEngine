# Tech Context: AeroBoarEngine

## Core Tech Stack
- **Language**: C++ (strong preference for C-style structs and static functions)
- **Graphics API**: Vulkan 1.3+
- **VR Platform**: Meta Quest 3 (Adreno 740, TBDR architecture)
- **Memory Management**: VulkanMemoryAllocator (VMA)

## Key Libraries & Extensions
- **Vulkan Extensions**:
  - `VK_KHR_multiview` (mandatory for stereo)
  - `VK_EXT_descriptor_indexing` (for bindless rendering)
- **Math**: GLM
- **Build System**: CMake + Android NDK
- **Shader Compilation**: `glslc` → SPIR-V (shaders in `shaders/` folder)
- **GLTF**: tinygltf used for loading models

## Coding Conventions
- Prefer **C-style structs** and static functions over class hierarchies (data-oriented design).
- **Naming**: `PascalCase` for structs/types, `snake_case` for functions/variables.
- All GPU memory allocated via **VMA**.
- Use transient/lazily allocated memory for MSAA color and depth to stay in GMEM.
- CPU-side data (e.g., `core::SceneInstance`) should be lightweight and fit in CPU cache.

## Architecture Principles

### Render Pipeline
- Single `VkRenderPass` with 2–4 subpasses (opaque + transparent/post-processing).
- Single `VkPipelineLayout` shared by all pipelines.
- One uber shader per subpass.
- Heavy use of input attachments and `VK_ATTACHMENT_STORE_OP_DONT_CARE` for on-chip efficiency.
- GPU-driven: Compute culling → indirect draw buffers → single `vkCmdDrawIndexedIndirectCount`.

### Material System
- Simple flat material struct (materialID + PBR params + bindless texture indices).
- Very low material variety → material variation handled via materialID + bindless indexing inside uber shaders.

### Resource Lifetime Rules

**1. Vulkan Global (created once)**
- `VkInstance`, `VkPhysicalDevice`, `VkDevice`
- `VkPipelineLayout` (single)
- `VkRenderPass` (single multiview)
- Descriptor set layout + bindless descriptor set
- All shader modules and `VkPipeline` objects
- `VkSampler` objects

**2. Per Pass**
- `VkFramebuffer` (one per swapchain image)
- Fixed foveated density map image (if used)

**3. Per Subpass**
- Nothing heavy. Subpasses share render pass, pipeline layout, and bindless set.
- Only switch `VkPipeline` (still using same layout) on `vkCmdNextSubpass`.

**4. Per Frame in Flight** (`MAX_FRAMES_IN_FLIGHT = 2`)
- Command buffers
- Per-frame scene data buffers
- Indirect draw buffers + draw count buffers
- Animation & culling storage buffers
- Synchronization objects (fences, semaphores)

**Rule of thumb**: Immutable interface objects → global. CPU-written data used by GPU → per-frame-in-flight.

## Compute Passes
- Separate `ComputePassContext` for culling and animation.
- Run before graphics render pass in the same command buffer.
- Use pipeline barriers (not extra semaphores) between compute and graphics.

## Synchronization & Frame Handling
- Use double buffering (`MAX_FRAMES_IN_FLIGHT = 2`).
- Per-frame synchronization via `VkFence` to prevent CPU overwriting in-flight data.
- Deferred destruction for resources still in use by the GPU.

## Build & Deployment
- Target: Android (Quest 3 / aarch64)
- Shaders automatically compiled to `build/shaders/*.spv`
- Optimize for Adreno GMEM / tile-based rendering

This document serves as the single source of truth for all major architectural decisions.

## Bindless Resource Management
- **Descriptor Sets**: One large descriptor set layout for all bindless resources.
- **Buffer Layouts**:
  - `VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` for `MeshSSBO` and `MaterialSSBO`.
  - `VkDescriptorType::VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER` for auxiliary data (e.g., instance transforms if not in buffer).
- **Update Frequency**: Descriptor set updates are rare (only on resource add/remove). Buffer content updates are frequent but handled via VMA mapped memory or staging buffers.

## Data Structures
- **`core::AABB`**:
  - Represents an Axis-Aligned Bounding Box in local or world space.
  - Used for culling and intersection tests.
- **`core::MeshData`**:
  - Struct for GPU-side mesh information.
  - Contains `local_aabb`, vertex/index offsets, counts, and stride.
- **`core::SceneInstance`**:
  - CPU-side struct for managing scene objects.
  - Contains world-space AABB, transform, and indices into `MeshSSBO` and `MaterialSSBO`.