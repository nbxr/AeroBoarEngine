---
description: "Minimal pipeline description"
applyTo: "src/*.h,src/*.cpp,src/*.hpp,src/*.inl"
---

# Quest 3 Vulkan Pipeline Implementation Guide (2026)

Title: GPU-Driven Bindless Rendering Pipeline for Meta Quest 3  
Target Hardware: Meta Quest 3 (Adreno 740)  
Vulkan Version: 1.3  
Rendering Style: Forward+ with GPU-driven culling and multiview stereo  
Goal: 72–120 FPS at 2064×2208 per eye with minimal CPU overhead

## 1. Core Principles

Minimize full render passes to avoid expensive GMEM to system memory round-trips on TBDR hardware.  
Use subpasses to keep color, depth, and intermediates on-chip with input attachments and dont-care store operations.  
Multiview is mandatory for stereo rendering.  
GPU-driven flow: compute culling writes indirect buffers, followed by a single indirect draw call.  
Bindless approach: one giant descriptor set bound once per frame.  
Use transient attachments so MSAA color and depth stay on-chip.

## 2. Recommended Render Pass Structure

Use one primary multiview render pass with two to four subpasses.

Attachments include a transient multisampled color, a resolved color that gets stored, and a transient depth attachment.

First subpass handles opaque geometry with GPU-driven indirect draws.  
Second subpass handles transparent geometry, lighting, or post-processing using input attachments to read previous results.  
Only the final subpass performs the resolve and stores the result.

Use subpass dependencies with input attachment read access to keep execution tiled and on-chip.  
Apply a view mask for the two eyes.

## 3. Bindless Descriptor Strategy

Use descriptor indexing with one large descriptor set marked partially bound and update after bind.  
Include large runtime descriptor arrays.  
Bind the set once at the start of the render pass.  
Access resources in shaders with non-uniform indexing. Scalarize where possible to reduce divergence on Adreno.

## 4. Recording Flow

Begin the render pass.  
Bind the bindless descriptor set once.  
Bind the pipeline.  
Issue a single indirect draw call.  
Advance to next subpass for transparent or post work using input attachments.  
End the render pass.

## 5. Optimizations

Use transient and lazily allocated images for multisampled color and depth.  
Prefer dont-care store operations everywhere possible.  
Add fixed foveated rendering via fragment density map.  
Avoid writing fragment depth to preserve hierarchical Z.  
Profile with Meta GPU Profiler and RenderDoc, watching for unnecessary load/store traffic.

## 6. Pipeline Layout and Shaders

Use a single pipeline layout with the bindless set plus push constants.  
Declare large runtime arrays in shaders for textures and buffers.

This setup delivers best-practice on-chip efficiency for GPU-driven VR on Quest 3.

## 7. Shader Strategy

Use a single super shader (uber shader) per subpass.
This minimizes pipeline binds and reduces total pipeline count, which is ideal for Quest 3's GPU-driven indirect rendering.
Material variation is handled at runtime via a material ID and bindless descriptor indexing instead of pipeline switches.
One uber shader for the opaque subpass and one for the transparent/post-processing subpass is sufficient given the low shading variety.
Branching cost remains acceptable on Adreno when variety is intentionally limited.
