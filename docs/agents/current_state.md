# Current State

Lightweight snapshot of the AeroBoarEngine project status. Intended to be read quickly by any agent.

## Overall Status

Early foundation phase. A basic PBR forward renderer is now implemented and active. The engine can load glTF scenes (including emissive and separate AO textures), upload everything into bindless resources, and render using a simple PBR shader with per-primitive draw calls. The current active asset (DamagedHelmet) only contains a single material, which limits visual variety.

## Major Completed Areas

- Project structure and build system (CMake + FetchContent dependencies)
- Vulkan instance, device, swapchain, and basic pipeline scaffolding
- VMA integration for memory management
- glTF loading via tinygltf (`GltfLoader`)
- Staged resource managers (Mesh, Texture, Material)
- Double-buffered buffer patterns for upload vs render
- Complete bindless GPU upload path at load time (all data visible in shaders)
- Basic but functional render loop (`Engine::render()`): acquire, record, bind bindless set, multiple indexed draws, submit, present
- Basic PBR forward shader (`pbr.vert` / `pbr.frag`) that samples:
  - Albedo (baseColor)
  - Normal map
  - Metallic + Roughness (from metalRoughness texture or factors)
  - Emissive
  - Ambient Occlusion (separate texture)
- Desktop `scene::Camera` system (fully documented in `src/scene/Camera.h`):
  - Quaternion-based 6DOF orientation (full roll support).
  - WASD: Move forward/back + strafe relative to current orientation.
  - Space / Left-Shift: Move along the camera's local up / down.
  - Mouse (when captured):
    - X axis: Yaw (rotate left/right) around the camera's current Up vector.
    - Y axis: Pitch (direction controlled by public `invert_pitch` flag; default = false = normal/non-inverted behavior).
  - Q / E: Roll the camera counterclockwise / clockwise around its forward axis.
  - R: Frame the view on the currently loaded scene (AABB-based).
  - Escape: Toggle mouse capture (with safe mouse state reset).
  - Public tunables: `movement_speed`, `mouse_sensitivity`, `invert_pitch`, `fov_degrees`, `near_plane`, `far_plane`.
  - Note: The implementation contains personal sign adjustments chosen for comfortable desktop model inspection. The documented public behavior above is the intended interface.
- `AGENTS.md` and shared documentation in `docs/agents/`

## Current Focus Areas

- Improving PBR quality (currently using very simple overhead directional lighting + constant ambient)
- Fixing remaining visual issues with the current asset (the loaded DamagedHelmet only contains a single material/primitive)
- Cleaning up the temporary debug rendering path (raw SSBO vertex pulling, forced depth hack in debug shader)
- Removing or properly guarding the diagnostic material coloring code
- Adding proper vertex input attributes (currently still using raw SSBO pulling)

## Known Gaps / Not Yet Implemented

- PBR lighting is extremely basic (single overhead directional light + constant ambient). No IBL, no multiple lights.
- The currently configured DamagedHelmet asset only contains a single material/primitive, so all geometry uses the same textures.
- Still using raw SSBO vertex pulling in shaders (no proper vertex input attributes).
- No instancing or indirect draws yet (CPU loop of `vkCmdDrawIndexed` per primitive).
- The old debug shader (`debug_draw.*`) still exists but is not the active pipeline.
- No compute culling pass
- No indirect drawing
- No OpenXR / VR input layer (desktop GLFW only)
- Scene model is still the simple flat `SceneInstance` (the full `GameObject` / `RenderMesh` + `TransformManager` SOA is future work)
- No physics, audio, or higher-level input abstraction

## Next Immediate Priorities

- Improve PBR lighting model (add IBL or at least better multi-light support)
- Switch from raw SSBO vertex pulling to proper vertex attribute input
- Investigate why the configured DamagedHelmet only has a single material (asset vs loading issue)
- Add basic instancing or move toward indirect draws
- Remove or clean up remaining debug/diagnostic code in the PBR path
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

A basic PBR forward renderer is now active:

- The pipeline uses `pbr.vert` + `pbr.frag`.
- Supports albedo, normal, metal/roughness, emissive, and separate AO textures via bindless sampling.
- Per-primitive material selection works via push constants.
- The render loop successfully draws the loaded scene with correct transforms.

**Important limitation**: The specific DamagedHelmet asset currently configured only contains a single material. This is the main reason multiple distinct textures are not visible on different parts of the model.

The implementation is still on a CPU-driven path (one draw call per primitive, raw SSBO vertex pulling). Moving to proper vertex attributes, instancing, and GPU-driven culling remains future work.

## Code Hygiene Follow-ups

These are non-functional cleanup items identified after the namespace reorganization:

- Replace the global `LOG_ERROR` / `LOG_INFO` macros (currently in `gfx/Engine.h`) with a proper namespaced logging utility.
- Reduce duplication across `MaterialManager`, `MeshManager`, `TextureManager`, and `SceneManager` (double-buffering, recycling, and buffer growth logic is nearly identical).
- Standardize cache / storage naming across the resource managers (currently a mix of `cpu_materials`, `mesh_cache`, `texture_cache`, etc.).
- Replace the raw `#define INVALID_HANDLE` in `core/Handle.h` with a `constexpr` constant.
- Reduce unnecessary `gfx::` qualification on types when already inside `namespace gfx`.
- Consider whether the top-level `AeroBoar` stub class is still needed.

Update this file when major phases complete or the focus shifts significantly.