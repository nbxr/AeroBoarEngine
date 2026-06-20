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
  - Escape: Toggle mouse capture (robust jump prevention handled by InputManager).
  - Public tunables: `movement_speed`, `mouse_sensitivity`, `roll_speed` (Q/E roll), `invert_pitch`, `fov_degrees`, `near_plane`, `far_plane`.
  - Note: The implementation contains personal sign adjustments chosen for comfortable desktop model inspection. The documented public behavior above is the intended interface.
- `core::InputManager` (desktop GLFW input layer):
  - High-precision mouse delta tracking via `glfwSetCursorPosCallback` (sub-frame accumulation).
  - Per-frame processing: non-linear mouse acceleration + EWMA temporal smoothing.
  - Keyboard + mouse button state.
  - Centralized cursor capture handling (`set_cursor_captured`) with automatic tracking reset to prevent jumps on Escape toggles.
  - Decouples raw input from `scene::Camera` (Camera now receives an `InputManager&` and queries processed deltas/keys).
  - See `docs/architecture/desktop-inputs.md` for full design rationale.
- `AGENTS.md` and shared documentation in `docs/agents/`

## Current Focus Areas

- Improving PBR quality (proper GGX BRDF + scene `KHR_lights_punctual` lights). The current `FrameGlobals` UBO (binding 0) + fixed `MAX_LIGHTS` arrays + std140 padding accommodations in `gfx::FrameGlobals` is a temporary stopgap implementation. The next step in the project is to implement a proper lighting solution (small per-frame constants + dedicated lights SSBO/buffer with clean layout, dynamic updates, better scalability, and full IBL integration). See `docs/architecture/lighting-implementation.md`.
- Fixing remaining visual issues with the current asset (the loaded DamagedHelmet only contains a single material/primitive)
- Cleaning up the temporary debug rendering path (raw SSBO vertex pulling, forced depth hack in debug shader)
- Removing or properly guarding the diagnostic material coloring code
- Adding proper vertex input attributes (currently still using raw SSBO pulling)

## Known Gaps / Not Yet Implemented

- PBR lighting: We have a solid GGX BRDF. glTF `KHR_lights_punctual` lights present in the loaded scene are now the active source (extracted + world-transformed from their nodes during load using the node's final composed matrix, written to the temporary `FrameGlobals` UBO at binding 0). Engine global directional is the automatic fallback for scenes without lights.

  **Important**: The current `FrameGlobals` UBO implementation (fixed-size light arrays, std140 layout workarounds in C++ to match GLSL `uniform` block padding rules for scalar arrays, direct mapped writes every frame, and incomplete double-buffering) is explicitly a temporary scaffolding solution. The next major step for the project is to implement a proper lighting solution. Recommended direction: a small, naturally-aligned per-frame constants UBO + a dedicated lights buffer (SSBO for std430/flexible layout and dynamic updates), support for larger light counts, proper dynamic light mutation, full production IBL, and better integration with the future scene/GameObject model. See `docs/architecture/lighting-implementation.md` for the current (temporary) model, known rough edges, and roadmap.

  **Note (end of session)**: The data path for scene lights is working (count + per-light values are logged and reach the shader). However, on the current `DamagedHelmetScene.gltf` asset the point light produces no visible illumination. The user is actively investigating glTF import / hierarchical transform correctness for lights (and cameras). (Cameras.gltf camera nodes and transforms now reproduce the authored poses and look directions correctly after fixing default TRS handling.) A global `BLENDER_CORRECTION` matrix that had been added experimentally was fully removed; the engine is now strictly faithful to the transforms present in the glTF file. Temporary rich diagnostic logging for light values and node hierarchy remains in place to support the investigation.
- The currently configured DamagedHelmet asset only contains a single material/primitive, so all geometry uses the same textures.
- Still using raw SSBO vertex pulling in shaders (no proper vertex input attributes).
- No instancing or indirect draws yet (CPU loop of `vkCmdDrawIndexed` per primitive).
- The old debug shader (`debug_draw.*`) still exists but is not the active pipeline.
- No compute culling pass
- No indirect drawing
- No OpenXR / VR input layer (desktop GLFW only)
- Scene model is still the simple flat `SceneInstance` (the full `GameObject` / `RenderMesh` + `TransformManager` SOA is future work)
- No physics, audio, or higher-level input abstraction (desktop input layer is now complete via `core::InputManager`)

## Next Immediate Priorities

- Lighting data path for `KHR_lights_punctual` is implemented and active via the temporary `FrameGlobals` UBO. However, scene lights are not yet producing correct visual results on the current asset. The user is investigating why assets (DamagedHelmetScene + its point light) are not importing correctly (transforms, world-space positions, intensity handling). A previous global Blender axis correction was removed; strict fidelity to glTF node data is now policy. See the note in the PBR lighting gap above and `docs/architecture/lighting-implementation.md`.
- The current FrameGlobals + fixed light array + std140 padding approach is temporary scaffolding. Once the data path and import issues stabilize, the explicit next step is to design and implement a proper lighting solution (small per-frame UBO + dedicated lights SSBO/buffer, clean layout without hacks, dynamic lights, larger counts, full IBL, etc.).
- Remaining lighting polish (inverse-square falloff for range=0 point lights, spot cone math, IBL, etc.) is intentionally deferred until the proper architecture is in place.
- [done] Switch from raw SSBO vertex pulling to proper vertex attribute input (pipeline + pbr.vert updated; vertex buffers now bound with VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
- Investigate why the configured DamagedHelmet only has a single material (asset vs loading issue)
- Add basic instancing or move toward indirect draws
- Remove or clean up remaining debug/diagnostic code in the PBR path
- Begin planning the transition from flat `SceneInstance` toward the full game-object model + GPU-driven culling
- [done] Desktop input layer: `core::InputManager` (callback-driven deltas + EWMA + acceleration + capture state) + full decoupling from `scene::Camera` (see `docs/architecture/desktop-inputs.md` and the implementation plan). Pitch sign convention restored to original comfortable default.
- glTF loader robustness: `extract_mesh_data` now accepts primitives that provide only POSITION (common in minimal test assets). Missing NORMAL defaults to (0,0,1); missing TEXCOORD_0 defaults to (0,0). This allows the Cameras.gltf pure-camera test scene (and similar) to load and render its proxy geometry. Also injects a default white material when the glTF contains no materials array (primitives may still reference default material via -1).
- Node transform extraction (`extract_node_transform`): now correctly defaults absent translation (0,0,0), rotation (identity quat), and scale (1,1,1) per glTF spec. Previous `value_or_ident` always supplied 1.0 which placed nodes incorrectly for assets like Cameras.gltf that omit TRS keys on some nodes (e.g. camera nodes with only translation, mesh nodes with only rotation). The helper was removed as dead after the fix. Quat component order also corrected for glTF [x,y,z,w] layout.

## GPU Upload Path (Completed)
The one-time scene upload at load is now fully wired:
- Bindless descriptor set is allocated with correct variable-count + update-after-bind flags.
- Layout declares the global tables (see tech_context.md for the exact binding numbers).
- Double-buffered managers (Scene, Material, Mesh) flip + bind their data after GltfLoader populates CPU side.
- Textures create images + upload via transfer queue + bind into the array.
- Minor bugs (ssbo accumulation, image_infos indexing, missing sampler, missing features) fixed as part of making the path executable.

The data is now in descriptors and ready for the render pass / shader work. Future dynamic updates will need per-frame-in-flight fencing + dirty tracking.

Per-frame bindless descriptor sets (one per `MAX_FRAMES_IN_FLIGHT`) are now used for everything, including binding 0 (the per-frame `FrameGlobals` UBO). Static resources are bound to all sets at load time; per-frame data (globals) is updated only on the matching set using `current_frame`. A small helper `bind_frame_globals_to_all_sets()` encapsulates the initial binding. Legacy `globals_upload`/`globals_render` fields were removed.

## Rendering Milestone (Achieved)

A basic PBR forward renderer is now active:

- The pipeline uses `pbr.vert` + `pbr.frag`.
- Supports albedo, normal, metal/roughness, emissive, and separate AO textures via bindless sampling.
- Per-primitive material selection works via push constants.
- The render loop successfully draws the loaded scene with correct transforms.

**Important limitation**: The specific DamagedHelmet asset currently configured only contains a single material. This is the main reason multiple distinct textures are not visible on different parts of the model.

The implementation is on a CPU-driven path (one draw call per primitive). Raw SSBO vertex pulling has been replaced by proper vertex attribute input (gfx::Vertex layout, matching pipeline state + pbr.vert attributes). Instancing and GPU-driven culling remain future work.

## Code Hygiene Follow-ups

These are non-functional cleanup items identified after the namespace reorganization:

- [done] Replace the global `LOG_ERROR` / `LOG_INFO` macros (in `gfx/Engine.h`) → moved to `core/Log.h` (lightweight, call sites unchanged).
- Reduce duplication across `MaterialManager`, `MeshManager`, `TextureManager`, and `SceneManager` (double-buffering, recycling, and buffer growth logic is nearly identical).
- Standardize cache / storage naming across the resource managers (currently a mix of `cpu_materials`, `mesh_cache`, `texture_cache`, etc.).
- [done] Replace the raw `#define INVALID_HANDLE` in `core/Handle.h` with a `constexpr` constant.
- Reduce unnecessary `gfx::` qualification on types when already inside `namespace gfx`.
- Consider whether the top-level `AeroBoar` stub class is still needed.
- [done] Complete per-frame bindless descriptor consistency for globals (binding 0): legacy `globals_upload`/`globals_render` removed, `bind_frame_globals_to_all_sets()` helper added, all sets now correctly paired with their buffers.

## Debugging & Diagnostics
- `core/Log.h` now supports a persistent log file (`aero_boar.log`). The file is explicitly opened relative to `std::filesystem::current_path()` (the process working directory) at startup. This respects the project's CMake setting `VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"`, so under normal debugging the log lands in the `build/` folder. At init time the code prints the **full absolute path** to both the console and the Visual Studio / VS Code debugger Output window so you always know exactly where the validation crash log went.
- Replaced vk-bootstrap's default debug messenger with a custom `VkDebugUtilsMessengerEXT` callback (`vulkan_debug_callback` in Engine.InitializeDevices.cpp) that funnels every validation layer message (errors, warnings, info, loader messages, etc.) through the logger. This ensures the exact validation text that precedes a crash (e.g. the DEVICE_LOST / bindless / sync issues you are hunting) is retained in the log file even when stdout/stderr buffers are lost.
- The log is enabled unconditionally at the very start of `main()`; no new config or command-line flags were added (kept minimal per project rules).
- The messenger is still cleaned up via `vkb::destroy_debug_utils_messenger` in the normal destroy path.

Update this file when major phases complete or the focus shifts significantly.