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

- Completing GPU data upload paths (meshes, materials, instances into SSBOs)
- Implementing the main render loop
- Wiring PBR shaders and bindless resources
- Getting the first real draws on screen

## Known Gaps / Not Yet Implemented

- Main render loop is empty / placeholder
- No compute culling pass
- No indirect drawing
- PBR shaders exist but are not integrated
- No OpenXR / VR input layer (desktop GLFW only)
- Scene graph / transform system is incomplete
- No physics, audio, or input abstraction

## Next Immediate Priorities

- Implement GPU buffer uploads for meshes and materials
- Get basic geometry rendering working end-to-end
- Stabilize the render loop with proper frame pacing

Update this file when major phases complete or the focus shifts significantly.