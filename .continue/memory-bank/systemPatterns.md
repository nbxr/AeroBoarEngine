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