# Quest 3 Vulkan Pipeline Implementation Guide (2026 Season)

Title: GPU-Driven Bindless Rendering Pipeline for Meta Quest 3  
Target Hardware: Meta Quest 3 (Snapdragon XR2 Gen 2 / Adreno 740)  
Vulkan Version: 1.3+ (Roadmap 2026 features where available)  
Rendering Style: Forward+ with GPU-driven culling, multiview stereo, heavy on-chip tile memory (GMEM/TBDR) usage  
Bindless Model: Classic descriptor indexing today, migrating toward VK_EXT_descriptor_heap when supported on Adreno  
Goal: 72–120 FPS at 2064×2208 per eye with minimal CPU overhead and maximum draw count

## Status / Target vs Current
**This is target / aspirational guidance for the 2026 Quest 3 pipeline.**

As of late May 2026:
- A basic but functional render loop exists and successfully draws complete glTF scenes (all primitives, correct per-instance transforms) using the bindless resources.
- Single-subpass render pass with transient MSAA + `DONT_CARE` is in place.
- Multi-subpass structure, multiview, compute culling, indirect draws, and real PBR shading are **not yet implemented**.
- Current drawing uses a temporary debug shader (raw SSBO vertex pulling).

See `docs/agents/current_state.md` for the latest implementation status.

## 1. Core Principles for Quest 3

Minimize full render passes. Every store operation that writes to system memory followed by a load in the next pass forces expensive round-trips from on-chip GMEM to DRAM on TBDR hardware.

Prefer subpasses. Keep color, depth, and intermediate data entirely on-chip using input attachments and dont-care store operations.

Multiview is mandatory. Leverage multiview for efficient left and right eye rendering.

GPU-driven everywhere. Perform compute culling and occlusion, write indirect draw buffers, then issue a single indirect draw call.

Compute-based culling: Before the rasterization pass, GPU compute shaders perform culling to reduce geometry throughput. This includes both view frustum culling and HZB (Hierarchical Z-Buffer) occlusion culling based on the previous frame depth buffer.

Bindless heap approach. Use one giant descriptor set bound once per frame today, or a true descriptor heap when the extension becomes available.

Transient attachments. MSAA color and depth should almost never leave on-chip memory.

## 2. Recommended Render Pass Structure

Use one primary multiview render pass with two to four subpasses for maximum tile efficiency.

Attachments consist of a transient multisampled color, a resolved color that is actually stored, and a transient depth attachment.

The first subpass handles opaque geometry with GPU-driven indirect draws.

The second subpass handles transparent geometry, lighting, or post-processing, reading previous results through input attachments.

Only the final subpass performs the resolve and stores the result.

Use proper subpass dependencies with input attachment read access so the driver keeps execution tiled and on-chip.

Multiview uses a view mask for the two eyes.

## 3. Bindless Descriptor Strategy

Current recommended path for Quest 3 (2026) uses descriptor indexing, which has been core since Vulkan 1.2.

Create one or very few large descriptor sets with partially bound and update after bind flags plus large runtime descriptor arrays.

Bind the set once at the start of the render pass.

Access resources in shaders via non-uniform indexing.

Note that non-uniform indexing can cause divergence on Adreno, so scalarize or keep control flow uniform where possible.

Future path is VK_EXT_descriptor_heap, released in January 2026 as part of the Vulkan Roadmap 2026 milestone.

It provides a true heap model with separate resource and sampler heaps.

Descriptors are written directly into backing memory, either from the CPU or GPU side.

Binding happens by device address and offset, which is extremely lightweight.

Current status on Quest 3 is that the extension is not yet exposed by Adreno drivers.

Recommendation is to implement a compile-time or runtime switch between descriptor indexing today and descriptor heaps when Meta and Qualcomm add support.

The extension is designed with backward compatibility in mind.

## 4. Recording Flow (GPU-Driven)

Begin the render pass.

Bind the bindless descriptor set or future heap once.

Bind the pipeline.

Issue a single indirect draw call, optionally using draw count for GPU-driven draw count.

Advance to the next subpass for transparent or post-processing work using input attachments.

End the render pass.

## 5. Quest 3-Specific Optimizations

Use transient and lazily allocated images for multisampled color and depth.

Prefer dont-care store operations wherever possible.

Add fixed foveated rendering through the fragment density map extension or Qualcomm-specific tile offset extensions.

Avoid writing to fragment depth as it disables hierarchical Z and low-resolution Z.

Profile using the Meta GPU Profiler, RenderDoc, and systrace. Pay special attention to unnecessary load and store traffic and sequential bin warnings.

Multi-draw indirect has only minor overhead on Quest 3.

## 6. Pipeline Layout and Shaders

Use a single pipeline layout for the entire frame consisting of the bindless set plus push constants for any per-draw parameters.

In shaders, declare large runtime arrays for textures, buffers, and other resources.

For the future descriptor heap path, access will use heap-relative indices.

## 7. Migration and Fallback Strategy

Today, implement with descriptor indexing and the single multiview subpass render pass.

When the extension becomes available, add the descriptor heap path with minimal code changes.

Keep render pass creation flexible so specialized passes for shadows, UI, or other effects can be added without rebuilding every frame.

## 8. References

Vulkanised 2026 talk on GPU-Driven Rendering in the Quest 2 and 3.

Meta Horizon OS Vulkan documentation.

Qualcomm Adreno Best Practices.

Khronos Vulkan Roadmap 2026 and the VK_EXT_descriptor_heap proposal.

Advanced GPU Pipelines and Loads/Stores section in Meta Developer Docs.

This document represents target 2026 architecture for high-performance GPU-driven VR on Quest 3: aggressive subpass usage for on-chip efficiency combined with modern bindless techniques that are evolving toward true descriptor heaps.

Maintained as of April 2026.

Update this file when VK_EXT_descriptor_heap becomes available on Quest 3 or when new Adreno tile-memory extensions ship.