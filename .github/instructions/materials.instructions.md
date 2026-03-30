---
description: Material system implementation plan
applyTo: "src/*.h,src/*.cpp,src/*.hpp,src/*.inl"
---

# Overview

You are helping me build a high-performance Vulkan material system for Meta Quest 3.

Project constraints:

- GPU-driven rendering with indirect draws
- Single uber shader per subpass (opaque and transparent/post)
- Heavy use of bindless descriptors via VK_EXT_descriptor_indexing
- Very low material/shading variety
- Target: maximum performance on Adreno 740 (Quest 3)
- Prefer legacy C-style coding when possible (structs, arrays, plain data)
- Use modern C++ features only when they clearly improve performance or safety (constexpr, std::array where beneficial, no STL containers in hot paths)

Core Material System Requirements:

A Material is defined by:

- uint32_t materialID;          // Index into global bindless arrays
- Shader parameters (PBR: albedo, roughness, metallic, emissive, etc.)
- Texture indices (albedo, normal, ORM, emissive, etc.) into bindless texture array
- Sampler indices (if needed)
- Flags / variant bits for minor uber-shader branching

Architecture preference:
Lean toward Architecture 1 (Simple Flat Material):

- One compact struct per material containing all data the uber shader needs.
- All materials stored in a single contiguous GPU buffer (SSBO) or large array.
- At runtime, culling writes materialID into indirect draw buffer.
- Uber shader reads material struct by materialID and indexes textures directly.
- Models will be loaded from GTLF files

Dynamic Material Creation Approach:
For dynamic material creation during runtime, implement double buffering to maintain performance:
- Maintain two material buffers (current and pending)
- Upload new materials to pending buffer in background
- Synchronize and switch buffers when upload completes
- Clean up old buffer after switch

Key rules for code:

- Use plain C structs with no virtual functions, no inheritance, no std::string in hot data structures; std::vector is allowed
- Prefer fixed-size arrays (std::array only if it helps alignment/performance).
- Keep structs tightly packed and cache-friendly (group frequently accessed data together).
- All material data should be uploadable to GPU in one go.
- Minimize indirection and pointer chasing in render loop.
- Use uint32_t and float32_t heavily for consistency with shaders.
- Avoid heap allocations per material after initial load.
- Provide functions like CreateMaterial(), UploadMaterialsToGPU(), GetMaterial().

Style:

- Write clean, readable C-style structs first.
- Use modern C++ only for compile-time constants, strong typing where it helps, or zero-overhead abstractions.
- Comment clearly why a decision improves Quest 3 performance.

When I ask for code, generate:

1. The main Material struct
2. Global material storage (array or buffer)
3. Functions to create and manage materials
4. How the materialID is used in the indirect draw path

Always prioritize:

- Minimal CPU overhead
- Cache-friendly data layout
- Zero runtime descriptor binding
- Compatibility with single pipeline + bindless indexing

Start by asking me for clarification only if truly needed. Otherwise, propose clean struct definitions first.
