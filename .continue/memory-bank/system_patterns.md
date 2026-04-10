# Architecture

This document outlines **engine‑wide architectural patterns** that shape the Vulkan rendering pipeline for Quest 3.  It does **not** contain language‑specific style rules – those belong in `techContext.md`.

## Quest 3 Vulkan Optimization Rules
1. **Subpasses** – Prefer subpasses over multiple render passes to keep data on‑chip (GMEM) and reduce DRAM bandwidth.
2. **Multiview** – Always enable `VK_KHR_multiview` for stereo rendering on Android; do not enable it for desktop builds.
3. **GPU‑Driven Culling** – Perform frustum/HZB culling in a compute pass and use indirect draw calls (`vkCmdDrawIndirect`) to keep work on the GPU.
4. **Bindless Strategy** – Use descriptor indexing (`VK_EXT_descriptor_indexing`) or a single large `VkDescriptorHeap` to hold many bindless descriptors.
5. **Transient Attachments** – Allocate MSAA depth, stencil, and colour attachments as `VK_ATTACHMENT_LOAD_OP_DONT_CARE` / `VK_ATTACHMENT_STORE_OP_DONT_CARE` whenever the data is not needed after the subpass.
6. **Minimise Stores** – Mark colour/depth attachments that are not read in a later subpass with `VK_ATTACHMENT_STORE_OP_DONT_CARE`.
7. **DRAM‑Traffic Reduction** – Avoid stores to attachments that are not consumed by a subsequent subpass; use `VK_ATTACHMENT_LOAD_OP_DONT_CARE` when appropriate.
8. **Memory Management** – Allocate and free images via **VulkanMemoryAllocator (VMA)**, using the *Transient* and *Lazily Allocated* allocation classes where possible.

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

### 2. Transformation Phase (Vertex Shader)
- **Input**: The `VisibleInstanceIndex` buffer.
- **Operation**: The Vertex Shader uses the index from the buffer to fetch the actual instance data (Transform matrices, material IDs) from the global object buffer.
- **Benefit**: The Vertex Shader never processes vertices for objects that failed the culling phase.

### 3. Rasterization & Fragment Phase
- **Operation**: Standard hardware rasterization.
- **Optimization**: Rely on **Subpass Dependencies** (if using Vulran/Vulkan) to keep tile-based memory (on-chip) local, avoiding expensive trips to Main Device Memory (LPDDR).