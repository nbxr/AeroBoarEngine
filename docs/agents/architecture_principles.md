# Architecture Principles

This document outlines the core architectural patterns and optimization principles for the AeroBoarEngine rendering pipeline, with a strong focus on Meta Quest 3.

## Quest 3 Vulkan Optimization Rules

1. **Subpasses** – Prefer subpasses over multiple render passes to keep data on-chip (GMEM) and reduce DRAM bandwidth.
2. **Multiview** – Use `VK_KHR_multiview` for stereo rendering on Android (do not enable for desktop development builds).
3. **GPU-Driven Culling** – Perform frustum and occlusion culling in a compute pass and use indirect draw calls to keep work on the GPU.
4. **Bindless Strategy** – Use descriptor indexing with large descriptor arrays (or a true descriptor heap when available) to minimize binding changes.
5. **Transient Attachments** – Allocate MSAA color and depth as transient / lazily allocated images whenever possible.
6. **Minimize Stores** – Use `VK_ATTACHMENT_STORE_OP_DONT_CARE` for attachments that are not read in a later subpass.
7. **DRAM Traffic Reduction** – Avoid writing data to system memory if it will only be consumed by the next subpass.
8. **Memory Management** – Allocate all GPU memory through VulkanMemoryAllocator (VMA), preferring transient and lazily allocated memory classes.
9. **GPU-Driven Work** – Push as much work as possible to the GPU (culling, animation, transform updates, etc.).

## Core Pipeline Philosophy

The engine is designed around a **Compute-First visibility pipeline**:

- A compute pass performs culling (frustum + occlusion) and produces an indirect draw buffer.
- The graphics pass consumes the results of the compute pass with minimal CPU involvement.
- The goal is to minimize vertex shader work on culled geometry and keep as much data as possible in on-chip memory (GMEM) on TBDR GPUs.

Detailed implementation choices (specific buffer layouts, shader structures, etc.) should be documented closer to the code and are expected to evolve.

## Bindless Resource Strategy

Use a bindless model with large descriptor arrays or descriptor indexing. Global SSBOs are preferred for frequently updated data such as transforms, materials, and mesh metadata. Keep data layouts simple, cache-friendly, and stable where possible.

Avoid over-specializing layouts early — they are expected to change as the engine matures.