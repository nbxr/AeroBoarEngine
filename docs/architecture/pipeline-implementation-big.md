# Quest 3 Vulkan Pipeline Implementation Guide (2026 Season)

Title: GPU-Driven Bindless Rendering Pipeline for Meta Quest 3  
Target Hardware: Meta Quest 3 (Snapdragon XR2 Gen 2 / Adreno 740)  
Vulkan Version: 1.3+ (Roadmap 2026 features where available)  
Rendering Style: Forward+ with GPU-driven culling, multiview stereo, heavy on-chip tile memory (GMEM/TBDR) usage  
Bindless Model: Classic descriptor indexing today, migrating toward VK_EXT_descriptor_heap when supported on Adreno  
Goal: 72–120 FPS at 2064×2208 per eye with minimal CPU overhead and maximum draw count

## 1. Core Principles for Quest 3

Minimize full render passes. Every store operation that writes to system memory followed by a load in the next pass forces expensive round-trips from on-chip GMEM to DRAM on TBDR hardware.

Prefer subpasses. Keep color, depth, and intermediate data entirely on-chip using input attachments and dont-care store operations.

Multiview is mandatory. Leverage multiview for efficient left and right eye rendering.

GPU-driven everywhere. Perform compute culling and occlusion, write indirect draw buffers, then issue a single indirect draw call.

Bindless heap approach. Use one giant descriptor set bound once per frame today, or a true descriptor heap when the extension becomes available.

Transient attachments. MSAA color and depth should almost never leave on-chip memory.

## 2. Recommended Render Pass Structure

Use one primary multiview render pass with the following pass sequence for maximum tile efficiency:

1. **Culling Pass (Pre-Render Pass)**
   - Execute before the main render pass begins
   - GPU-driven frustum culling and occlusion culling via compute shaders
   - Generates indirect draw buffers with only visible objects
   - Writes to a separate storage buffer, not part of the main render pass attachments

2. **Main Render Pass with Subpasses**
   
   a. **Opaque Geometry Subpass**
      - Handles opaque geometry with GPU-driven indirect draws
      - Uses multiview stereo rendering
      - Transient multisampled color and depth attachments
      - Store operations: dont-care for all transient attachments
   
   b. **Transparent/Lighting Subpass**
      - Handles transparent geometry, deferred lighting, or intermediate calculations
      - Reads previous subpass results through input attachments
      - Maintains data on-chip via subpass dependencies
   
   c. **Additional Subpasses (Optional)**
      - Can include up to 2-4 total subpasses depending on rendering needs
      - Each subpass should maintain on-chip residency

3. **GUI Pass (Post-Render Pass)**
   - Execute after the main render pass completes
   - Dedicated pass for UI elements, text rendering, and HUD elements
   - Reads resolved color from main pass as input attachment or separate load
   - Writes final composited result to swapchain
   - Can use a separate simple render pass with its own pipeline

Attachments consist of a transient multisampled color, a resolved color that is actually stored, and a transient depth attachment.

Only the final subpass performs the resolve and stores the result.

Use proper subpass dependencies with input attachment read access so the driver keeps execution tiled and on-chip.

Multiview uses a view mask for the two eyes.

## 3. Compute Shader Pipeline (Animation, IK, Physics)

### 3.1 Animation Compute Shaders

All character and object animations are performed in compute shaders before rendering:

- **Execute before the culling pass** or in parallel with culling if independent
- **Per-frame animation updates**:
  - Skeleton bone transformations
  - Morph target blending
  - Procedural animation adjustments
  - Animation state machine updates
- Output: Transformed bone matrices and skinning weights stored in GPU buffers
- Input: Base skeleton data, animation blend weights, time delta
- Use structured buffers for efficient memory access patterns on Adreno

### 3.2 Inverse Kinematics (IK) Compute Shader

IK calculations are performed in a dedicated compute shader stage:

- **Execute after forward animation but before culling**
- **Per-character IK solving**:
  - Foot placement and contact stabilization
  - Reach targets and constraint solving
  - Spine and limb adjustments
- Output: Modified bone transformations that override or blend with animated values
- Input: Animated bone poses, IK targets, constraint parameters
- Should be dispatched per character or in batches for efficiency

### 3.3 Physics-Based Mesh Deformation

Mesh deformation from physics interactions is handled via compute shaders:

- **Execute after IK but before culling**
- **Deformation types**:
  - Soft body deformation
  - Cloth simulation results
  - Physics skinning overrides
  - Procedural deformation (wind, water, etc.)
- Output: Vertex displacements or alternative vertex buffers
- Input: Physics simulation state, base mesh data, deformation parameters
- Can modify existing vertex buffers or write to alternative mesh variants

### 3.4 Compute Shader Integration Points

```
Frame Timeline:
1. CPU-side: Record compute command buffers for animation/IK/physics
2. GPU-side dispatch:
   a. Animation compute (forward animation)
   b. IK compute (solve constraints)
   c. Physics deformation compute
   d. Culling compute (visibility determination)
3. GPU-side dispatch:
   a. Main render pass with subpasses
   b. GUI pass
```

**Important Notes:**
- Compute shaders should use storage buffers for all input/output data
- Ensure proper synchronization barriers between compute and graphics pipeline stages
- Consider using separate compute and graphics queues if available, though Adreno typically uses a unified queue
- Profile compute shader execution time to ensure it doesn't exceed the frame budget

## 4. Bindless Descriptor Strategy

Current recommended path for Quest 3 in March 2026 uses descriptor indexing, which has been core since Vulkan 1.2.

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

## 5. Recording Flow (GPU-Driven)

1. **Begin Frame - Compute Stage**
   - Dispatch animation compute shaders
   - Dispatch IK compute shaders
   - Dispatch physics deformation compute shaders
   - Dispatch culling compute shader (reads transformed mesh data)
   - Culling compute writes to indirect draw buffers

2. **Begin Main Render Pass**
   - Bind the bindless descriptor set or future heap once
   - Bind the opaque geometry pipeline

3. **Opaque Subpass**
   - Issue a single indirect draw call using culling output, optionally using draw count for GPU-driven draw count

4. **Advance to Transparent/Lighting Subpass**
   - Use input attachments to read from previous subpass
   - Bind transparent pipeline if needed

5. **Additional Subpasses**
   - Continue with any additional rendering subpasses

6. **End Main Render Pass**
   - Resolve multisampled attachments
   - Store final color to intermediate target or directly to swapchain

7. **GUI Pass**
   - Separate render pass for UI elements
   - Bind GUI pipeline with simple orthographic projection
   - Render text, HUD elements, and overlay graphics
   - Store final composited result to swapchain

8. **Present**
   - Submit to present queue

## 6. Quest 3-Specific Optimizations

Use transient and lazily allocated images for multisampled color and depth.

Prefer dont-care store operations wherever possible.

Add fixed foveated rendering through the fragment density map extension or Qualcomm-specific tile offset extensions.

Avoid writing to fragment depth as it disables hierarchical Z and low-resolution Z.

Profile using the Meta GPU Profiler, RenderDoc, and systrace. Pay special attention to unnecessary load and store traffic and sequential bin warnings.

Multi-draw indirect has only minor overhead on Quest 3.

**Compute Shader Optimizations:**
- Batch related compute dispatches when possible
- Use shared memory (local workgroup memory) for IK and deformation algorithms
- Profile compute shader memory access patterns with GPU Profiler
- Consider compute shader clustering to reduce dispatch overhead

## 7. Pipeline Layout and Shaders

Use a single pipeline layout for the entire frame consisting of the bindless set plus push constants for any per-draw parameters.

In shaders, declare large runtime arrays for textures, buffers, and other resources.

For the future descriptor heap path, access will use heap-relative indices.

**Compute Shader Layout:**
- Separate pipeline layouts for animation, IK, and physics compute shaders
- Shared descriptor set for common resources (mesh data, material parameters)
- Push constants for per-dispatch parameters (time delta, simulation step)

## 8. Migration and Fallback Strategy

Today, implement with descriptor indexing and the single multiview subpass render pass.

When the extension becomes available, add the descriptor heap path with minimal code changes.

Keep render pass creation flexible so specialized passes for shadows, UI, or other effects can be added without rebuilding every frame.

**Compute Shader Migration:**
- Abstract compute shader dispatch behind a unified API
- Allow runtime switching between different compute implementations
- Profile and optimize compute shaders separately from graphics pipelines

## 9. Shader Strategy - Single Super Shader per Subpass

Given the very limited material and shading variety in this project, using a single super shader (uber shader) per subpass is the recommended approach.

This decision provides several important benefits for Quest 3:

- It minimizes pipeline binding overhead, which is especially valuable in a GPU-driven setup that relies on a single indirect draw call per subpass.
- It reduces the total number of Vulkan pipelines that need to be created and managed.
- It keeps the render pass structure simple and allows the Adreno driver to optimize tile memory usage more effectively.
- With bindless descriptor access and a per-draw material ID, variation between materials is handled inside the shader rather than by switching pipelines.

In practice this means:

- One super shader for the opaque geometry subpass.
- One super shader for the transparent, lighting, or post-processing subpass.

Material differences are resolved at runtime using a material identifier passed through the indirect draw buffer or instance data, combined with non-uniform indexing into the large bindless descriptor set.

Because the shading variety is intentionally kept low, the cost of controlled branching inside the shader remains acceptable and is generally cheaper than frequent pipeline changes on Quest 3.

This uber-shader approach aligns well with high-performance GPU-driven rendering patterns used on mobile VR hardware in 2026.

**Compute Shader Super Shader Considerations:**
- Consider using super shaders for compute paths as well (animation, IK, physics)
- Use dispatch-time parameters to select different computation modes
- Keep compute shaders modular with function-based organization for maintainability

## 10. References

Vulkanised 2026 talk on GPU-Driven Rendering in the Quest 2 and 3.

Meta Horizon OS Vulkan documentation.

Qualcomm Adreno Best Practices.

Khronos Vulkan Roadmap 2026 and the VK_EXT_descriptor_heap proposal.

Advanced GPU Pipelines and Loads/Stores section in Meta Developer Docs.

This document represents best-practice 2026 season architecture for high-performance GPU-driven VR on Quest 3: aggressive subpass usage for on-chip efficiency combined with modern bindless techniques that are evolving toward true descriptor heaps.

Maintained as of March 2026.

Update this file when VK_EXT_descriptor_heap becomes available on Quest 3 or when new Adreno tile-memory extensions ship.
