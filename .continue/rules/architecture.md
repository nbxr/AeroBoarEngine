---
globs: |-
  **/*.h
  **/*.hpp
  **/*.cpp
  **/*.inl
  **/*.glsl
  **/*.vert
  **/*.frag
  **/*.comp
description: Ensures all new Vulkan rendering implementations follow the
  high-performance GPU-driven architecture optimized for Meta Quest 3 (Adreno
  740).
alwaysApply: true
---

# Architecture

- Agent rules contained in `./.continue/rules`
- Memory bank contained in `./.continue/memory-bank/`
- Details contained in `./docs/architecture/pipeline-implementation.md`
- Shaders contained in `./shaders/`
- Source code contained in `./src/`

When implementing Vulkan rendering code, strictly follow these Quest 3 optimization standards:
1. Use Subpasses: Prefer subparts over multiple render passes to keep data on-chip (GMEM).
2. Use Multiview: Always use multiview for stereo rendering when targeting Android. Do not use it for desktop builds.
3. GPU-Driven: Implement compute-based culling (frustum/HZB) in a pre-compute pass and use indirect draw calls.
4. Bindless Strategy: Use descriptor indexing (or VK_EXT_descriptor_heap if available) with a single large descriptor set.
5. Transient Attachments: Use transient and lazily allocated images for MSAA/Depth to avoid DRAM round-trips.
6. Minimize Stores: Use VK_ATTACHMENT_STORE_OP_DONT_CARE for attachments not needed in subsequent passes.
7. Minimize DRAM Traffic: Avoid store operations that write to system memory if the data is only needed in the next subpass.
8. Memory management: Use VulkanMemoryAllocator for allocation and management.