# Current State

Lightweight snapshot of the AeroBoarEngine project status. Intended to be read quickly by any agent.

## Overall Status

Early-to-mid foundation phase. The engine has a working Vulkan + GLFW desktop skeleton with glTF loading, resource managers, and basic bindless descriptor setup. The main render loop and GPU-driven culling pipeline are not yet functional.

## Major Completed Areas

- Project structure and build system (CMake + FetchContent dependencies)
- Vulkan instance, device, swapchain, and basic pipeline scaffolding
- VMA integration for memory management
- glTF loading via tinygltf (`GltfLoader`)
- Staged resource managers (Mesh, Texture, Material)
- Double-buffered buffer patterns for upload vs render
- Basic scene loading from `assets/scenes/configuration.json`
- `AGENTS.md` and shared documentation in `docs/agents/`

## Current Focus Areas

- Implementing the main render loop (now that scene uploads are wired)
- Wiring PBR shaders and using the bindless resources in draws
- Getting the first real draws on screen (basic geometry + materials)

## Known Gaps / Not Yet Implemented

- Main render loop is empty / placeholder
- No compute culling pass
- No indirect drawing
- PBR shaders exist but are not integrated
- No OpenXR / VR input layer (desktop GLFW only)
- Scene graph / transform system is incomplete
- No physics, audio, or input abstraction

## Next Immediate Priorities

- Get basic geometry rendering working end-to-end (use the now-wired bindless SSBOs + textures from load)
- Stabilize the render loop with proper frame pacing and per-frame data
- Implement a minimal forward pass that actually issues draws using the uploaded scene data

## GPU Upload Path (Completed)
The one-time scene upload at load is now fully wired:
- Bindless descriptor set is allocated with correct variable-count + update-after-bind flags.
- Layout declares the global tables (see tech_context.md for the exact binding numbers).
- Double-buffered managers (Scene, Material, Mesh) flip + bind their data after GltfLoader populates CPU side.
- Textures create images + upload via transfer queue + bind into the array.
- Minor bugs (ssbo accumulation, image_infos indexing, missing sampler, missing features) fixed as part of making the path executable.

The data is now in descriptors and ready for the render pass / shader work. Future dynamic updates will need per-frame-in-flight fencing + dirty tracking.

## Code Hygiene Follow-ups

These are non-functional cleanup items identified after the namespace reorganization:

- Replace the global `LOG_ERROR` / `LOG_INFO` macros (currently in `gfx/Engine.h`) with a proper namespaced logging utility.
- Reduce duplication across `MaterialManager`, `MeshManager`, `TextureManager`, and `SceneManager` (double-buffering, recycling, and buffer growth logic is nearly identical).
- Standardize cache / storage naming across the resource managers (currently a mix of `cpu_materials`, `mesh_cache`, `texture_cache`, etc.).
- Replace the raw `#define INVALID_HANDLE` in `core/Handle.h` with a `constexpr` constant.
- Reduce unnecessary `gfx::` qualification on types when already inside `namespace gfx`.
- Consider whether the top-level `AeroBoar` stub class is still needed.

Update this file when major phases complete or the focus shifts significantly.