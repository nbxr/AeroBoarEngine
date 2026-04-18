# Architecture

This document outlines **engine‑wide architectural patterns** that shape the Vulkan rendering pipeline for Quest 3.  It does **not** contain language‑specific style rules – those belong in `techContext.md`.

## Quest 3 Vulkan Optimization Rules
1. **Subpasses** – Prefer subpasses over multiple render passes to keep data on‑chip (GMEM) and reduce DRAM bandwidth.
2. **Multiview** – Enable `VK_KHR_multiview` for stereo rendering on Android but do not enable it for desktop builds.
3. **GPU‑Driven Culling** – Perform frustum/HZB culling in a compute pass and use indirect draw calls (`vkCmdDrawIndirect`) to keep work on the GPU.
4. **Bindless Strategy** – Use descriptor indexing (`VK_EXT_descriptor_indexing`) or a single large `VkDescriptorHeap` to hold many bindless descriptors.
5. **Transient Attachments** – Allocate MSAA depth, stencil, and colour attachments as `VK_ATTACHMENT_LOAD_OP_DONT_CARE` / `VK_ATTACHMENT_STORE_OP_DONT_CARE` whenever the data is not needed after the subpass.
6. **Minimize Stores** – Mark colour/depth attachments that are not read in a later subpass with `VK_ATTACHMENT_STORE_OP_DONT_CARE`.
7. **DRAM‑Traffic Reduction** – Avoid stores to attachments that are not consumed by a subsequent subpass; use `VK_ATTACHMENT_LOAD_OP_DONT_CARE` when appropriate.
8. **Memory Management** – Allocate and free images via **VulkanMemoryAllocator (VMA)**, using the *Transient* and *Lazily Allocated* allocation classes where possible.
9. **GPU-Driven Animation** – Animation will be performed on GPU to include effects from physics interactions when possible. Use compute shaders to deform vertices in shader storage buffers (SSBOs). Dispatch once per frame: include vertex displacement from phsyics effects, add animation offsets (from buffer or procedural), then bind the updated SSBO as vertex buffer for rendering. This is GPU-only, zero CPU readback.

## References
- `techContext.md` – for required Vulkan extensions (e.g., `VK_KHR_multiview`, `VK_EXT_descriptor_indexing`) and VMA version.

## GPU Pipeline Flow: Compute-to-Graphics
To maximize throughput on mobile architectures (Adreno/Mali), the engine follows a "Compute-First" visibility pipeline. This minimizes vertex shader invocation for culled geometry.

### 1. Culling Phase (Compute Shader)
- **Input**: High-density buffer of object bounding volumes (AABBs/Spheres) and the current Camera Frustum.
- **Operation**: A Compute Shader executes one thread per object. It performs:
    - **Frustum Culling**: Checks visibility against the 6 frustum planes.
    - **Hi-Z Occlusion Culling** (Optional/Future): Checks against a downsampled depth buffer from the previous frame.
- **Output**: An `IndirectDrawCommand` buffer (containing `instanceCount`, `firstIndex`, etc.) and a `VisibleInstanceIndex` buffer.

### 2. Animation Phase (Compute Shader)
- **Input**: High-density buffer of object meshes and data required for animation.
- **Operation**: A Compute Shader performs animation computations including effects from physics interactions when relevant or purely animation otherwise.
- **Benefit**: Animation is performed efficiently on GPU instead of CPU.

### 3. Transformation Phase (Vertex Shader)
- **Input**: The `VisibleInstanceIndex` buffer.
- **Operation**: The Vertex Shader uses the index from the buffer to fetch the actual instance data (Transform matrices, material IDs) from the global object buffer.
- **Benefit**: The Vertex Shader never processes vertices for objects that failed the culling phase.

### 4. Rasterization & Fragment Phase
- **Operation**: Standard hardware rasterization.
- **Optimization**: Rely on **Subpass Dependencies** (if using Vulran/Vulkan) to keep tile-based memory (on-chic) local, avoiding expensive trips to Main Device Memory (LPDDR).

## Bindless Architecture

To support high draw call counts on mobile (Quest 3), we use a **Bindless Rendering** strategy.

### Data Layout
1. **`core::MeshSSBO`**:
   - Contains `core::MeshData` entries.
   - Each entry includes `local_aabb`, vertex/index offsets, and counts.
   - Accessed via descriptor indexing in vertex/geometry shaders.

2. **`core::MaterialSSBO`**:
   - Contains `core::MaterialData` entries.
   - Includes PBR parameters and bindless texture indices.
   - Accessed via descriptor indexing in fragment shaders.

3. **`core::SceneInstance`** (CPU-side):
   - Struct containing `core::AABB` (world space), transform, and indices into `MeshSSBO` and `MaterialSSBO`.
   - Used for culling and instance management.

### Flow
- **CPU**: Manages `SceneInstance` list. Updates world-space AABBs.
- **Compute**: Reads world-space AABBs, performs culling, writes visible indices to a draw indirect buffer.
- **Graphics**: Binds `MeshSSBO` and `MaterialSSBO` as descriptor sets. Uses indirect draw calls based on visible indices.