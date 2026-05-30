# Current State

Lightweight snapshot of the AeroBoarEngine project status. Intended to be read quickly by any agent.

## Overall Status

Early foundation phase with the first major rendering milestone achieved. The engine can load glTF scenes, upload all geometry/materials/textures into bindless GPU resources, and render the complete model using a working Vulkan render loop on desktop. Full scene geometry (all meshes/primitives with correct per-instance transforms) is now visible and interactive via a movable camera.

## Major Completed Areas

- Project structure and build system (CMake + FetchContent dependencies)
- Vulkan instance, device, swapchain, and basic pipeline scaffolding
- VMA integration for memory management
- glTF loading via tinygltf (`GltfLoader`)
- Staged resource managers (Mesh, Texture, Material)
- Double-buffered buffer patterns for upload vs render
- Complete bindless GPU upload path at load time (all data visible in shaders)
- Basic but functional render loop (`Engine::render()`): acquire, record, bind bindless set, multiple indexed draws, submit, present
- Multi-primitive / multi-mesh drawing: iterates all `SceneInstance`s and draws every mesh primitive with correct per-instance transforms via push constants
- Desktop `scene::Camera` system (WASD + mouse look, R to frame loaded scene)
- `AGENTS.md` and shared documentation in `docs/agents/`

## Current Focus Areas

- Cleaning up the temporary debug rendering path (raw SSBO vertex pulling, forced depth, magenta clear color)
- Integrating real PBR shading using the already-uploaded bindless materials and textures
- Removing debug-only instrumentation and hacks now that geometry is visible
- Stabilizing the render loop and camera on desktop before moving toward production shaders and culling

## Known Gaps / Not Yet Implemented

- Only a temporary debug shader is used (`debug_draw.vert`): raw float SSBO vertex pulling, hardcoded green, forced `gl_Position.z = 0.5`, no real lighting or materials
- No production PBR forward pass yet (shaders exist but are not wired)
- No compute culling pass
- No indirect drawing
- No OpenXR / VR input layer (desktop GLFW only)
- Scene model is still the simple flat `SceneInstance` (the full `GameObject` / `RenderMesh` + `TransformManager` SOA is future work)
- No physics, audio, or higher-level input abstraction

## Next Immediate Priorities

- Replace the debug draw path with real PBR shaders while keeping the same bindless data layout and multi-draw structure
- Remove temporary hacks (forced depth, magenta clear, raw vertex pulling, excessive debug prints)
- Add proper vertex input attributes and a clean material shading pass
- Begin planning the transition from flat `SceneInstance` toward the full game-object model + GPU-driven culling

## GPU Upload Path (Completed)
The one-time scene upload at load is now fully wired:
- Bindless descriptor set is allocated with correct variable-count + update-after-bind flags.
- Layout declares the global tables (see tech_context.md for the exact binding numbers).
- Double-buffered managers (Scene, Material, Mesh) flip + bind their data after GltfLoader populates CPU side.
- Textures create images + upload via transfer queue + bind into the array.
- Minor bugs (ssbo accumulation, image_infos indexing, missing sampler, missing features) fixed as part of making the path executable.

The data is now in descriptors and ready for the render pass / shader work. Future dynamic updates will need per-frame-in-flight fencing + dirty tracking.

## Rendering Milestone (Achieved)

Basic end-to-end scene rendering is now functional on the debug path:

- Full glTF geometry (all meshes and primitives) is drawn every frame.
- Correct per-`SceneInstance` transforms are applied via per-draw push constants.
- Camera movement and scene framing work.
- All previously uploaded bindless resources (vertices, indices, mesh metadata, instances) are being used in real draw calls.

This proves the complete load → upload → bindless → multi-draw pipeline. The current implementation still uses a temporary debug shader and will be replaced by production PBR shading.

## Code Hygiene Follow-ups

These are non-functional cleanup items identified after the namespace reorganization:

- Replace the global `LOG_ERROR` / `LOG_INFO` macros (currently in `gfx/Engine.h`) with a proper namespaced logging utility.
- Reduce duplication across `MaterialManager`, `MeshManager`, `TextureManager`, and `SceneManager` (double-buffering, recycling, and buffer growth logic is nearly identical).
- Standardize cache / storage naming across the resource managers (currently a mix of `cpu_materials`, `mesh_cache`, `texture_cache`, etc.).
- Replace the raw `#define INVALID_HANDLE` in `core/Handle.h` with a `constexpr` constant.
- Reduce unnecessary `gfx::` qualification on types when already inside `namespace gfx`.
- Consider whether the top-level `AeroBoar` stub class is still needed.

Update this file when major phases complete or the focus shifts significantly.