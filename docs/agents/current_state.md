# Current State

Lightweight snapshot of the AeroBoarEngine project status. Intended to be read quickly by any agent.

## Overall Status

Early foundation phase. A basic PBR forward renderer is implemented and active. The engine can load glTF scenes (including emissive and separate AO textures), upload everything into bindless resources, and render using a simple PBR shader with per-primitive draw calls. Default scene selection is via `assets/scenes/configuration.json` (currently often multi-material sample assets such as ABeautifulGame).

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
- Proper vertex attribute input (`gfx::Vertex`, pipeline vertex state, `pbr.vert`) — legacy SSBO vertex pulling is no longer the active path
- Materials SSBO correctly declared as a **single buffer + runtime array** in `pbr.frag` (not a descriptor array); `gfx::Material` is `alignas(16)` / 80-byte stride to match std430
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

- Move frustum cull to compute (write indirect + instances on GPU); `gl_BaseInstance` path
- Transform hierarchy propagate (local TRS SOA → world)
- Spot light direction packing, dynamic lights, HDR env loading
- Optional: remove or archive unused `debug_draw.*` path

## Known Gaps / Not Yet Implemented

- PBR lighting: GGX BRDF + `KHR_lights_punctual` (world transforms at load) or engine global directional fallback.

  **Lighting architecture (current)**:
  - Binding 0: small `FrameConstants` UBO (camera + exposure in `vec4.w`, lightCount, SH, IBL indices)
  - Binding 6: `GpuLight[]` lights SSBO (std430)
  - Binding 7–8: IBL cubemap + BRDF LUT; binding 9: bindless textures (variable, highest)
  - Auto-exposure accounts for point-light distance (`compute_auto_exposure` vs scene center) so Blender-export candela values (~54k) actually illuminate
  - KHR inverse-square falloff for range=0 point lights

  **IBL (Phase 3, landed)**: `gfx::IblEnvironment` bakes a procedural outdoor sky at init into:
  - 3-band SH irradiance → `FrameConstants.shCoefficients`
  - Prefiltered GGX cubemap (32², ~4 mips) → binding 7 `samplerCube`
  - BRDF integration LUT (128²) → binding 8 `sampler2D`
  - Split-sum specular in `pbr.frag` when IBL is ready
  Still deferred: load HDR equirect env assets, dynamic lights, clustered many-lights, complete spot packing.
- **Instancing + cull + indirect (landed)**:
  - Static `MeshDrawInfo` templates at load (geometry ranges + RenderMesh ids)
  - Per-frame **CPU frustum cull** of world AABBs (`core::Frustum`) → compact `DrawInstanceGPU[]` + `VkDrawIndexedIndirectCommand[]` (double-buffered)
  - `vkCmdDrawIndexedIndirect` per visible mesh batch; push `first_instance` as `pc.extra.x`
  - Device features: `multiDrawIndirect`, `drawIndirectFirstInstance` (ready for full GPU path)
  - Still TODO: compute cull writing indirect/instance buffers; `gl_BaseInstance` when glslc supports it
- The old debug shader (`debug_draw.*`) still exists but is not the active pipeline
- No OpenXR / VR input layer (desktop GLFW only)
- **Scene model (foundation landed)**: glTF load creates `GameObject` (per mesh node) + `RenderMesh` (per prim) + `TransformManager` world matrices. Instanced draws and camera framing use this path. Legacy `SceneInstance` dual-written. Log: `gameObjects=… renderMeshes=… transforms=…`. Still TODO: hierarchy propagate, GPU cull buffer, full SOA TRS.
- No physics, audio, or higher-level input abstraction (desktop input layer is now complete via `core::InputManager`)

## Next Immediate Priorities

- [done] Scene light visual correctness: distance-aware auto-exposure + physical inverse-square; load-time light logging
- [done] Proper lighting layout: `FrameConstants` UBO + lights SSBO (textures moved to binding 7)
- Remaining lighting polish: spot direction packing, HDR env load, dynamic lights
- [done] Production IBL (procedural env + SH + prefiltered cube + BRDF LUT)
- [done] Switch from raw SSBO vertex pulling to proper vertex attribute input
- [done] Materials SSBO GLSL shape + C++ 80-byte stride
- [done] PBR path hygiene
- [done] Camera framing
- [done] Mesh-grouped instancing (`DrawBatch` / `DrawInstanceGPU`)
- [done] GameObject + RenderMesh + TransformManager foundation
- [done] Frustum culling + multi-draw indirect (CPU cull; compute cull next)
- Next: compute frustum cull; transform hierarchy propagate
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
- Per-primitive material selection works via push constants (`extra.x` = material index into the materials SSBO).
- The render loop successfully draws the loaded scene with correct transforms.
- Multi-material scenes work once materials are indexed as elements of a single SSBO (see Materials SSBO note below).

The implementation is on a CPU-driven path (one draw call per primitive). Instancing and GPU-driven culling remain future work.

### Materials SSBO (important)

C++ binds **one** `STORAGE_BUFFER` for all materials (binding 2). The shader must declare a runtime array **inside** that buffer:

```glsl
struct Material { /* matches gfx::Material, 80-byte std430 stride */ };
layout(set = 0, binding = 2) readonly buffer Materials {
    Material materials[];
};
```

**Incorrect** (caused AMD `VK_ERROR_DEVICE_LOST` when any material index ≥ 1 was shaded):

```glsl
// This is an array of buffer *descriptors*, not elements in one buffer.
layout(...) readonly buffer Materials { ... } materials[];
```

`gfx::Material` uses `alignas(16)` so `sizeof(Material) == 80`, matching GLSL std430 array stride (fields end at 76; base align 16 → stride 80).

## Code Hygiene Follow-ups

These are non-functional cleanup items identified after the namespace reorganization:

- [done] Replace the global `LOG_ERROR` / `LOG_INFO` macros (in `gfx/Engine.h`) → moved to `core/Log.h` (lightweight, call sites unchanged).
- Reduce duplication across `MaterialManager`, `MeshManager`, `TextureManager`, and `SceneManager` (double-buffering, recycling, and buffer growth logic is nearly identical).
- Standardize cache / storage naming across the resource managers (currently a mix of `cpu_materials`, `mesh_cache`, `texture_cache`, etc.).
- [done] Replace the raw `#define INVALID_HANDLE` in `core/Handle.h` with a `constexpr` constant.
- Reduce unnecessary `gfx::` qualification on types when already inside `namespace gfx`.
- Consider whether the top-level `AeroBoar` stub class is still needed.
- [done] Complete per-frame bindless descriptor consistency for globals (binding 0): legacy `globals_upload`/`globals_render` removed, `bind_frame_globals_to_all_sets()` helper added, all sets now correctly paired with their buffers.
- [done] PBR diagnostic noise and accidental `extra.y` debug-mode pollution cleaned up (2026-08).

## Debugging & Diagnostics
- `core/Log.h` now supports a persistent log file (`aero_boar.log`). The file is explicitly opened relative to `std::filesystem::current_path()` (the process working directory) at startup. This respects the project's CMake setting `VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"`, so under normal debugging the log lands in the `build/` folder. At init time the code prints the **full absolute path** to both the console and the Visual Studio / VS Code debugger Output window so you always know exactly where the validation crash log went.
- Replaced vk-bootstrap's default debug messenger with a custom `VkDebugUtilsMessengerEXT` callback (`vulkan_debug_callback` in Engine.InitializeDevices.cpp) that funnels every validation layer message (errors, warnings, info, loader messages, etc.) through the logger. This ensures the exact validation text that precedes a crash (e.g. the DEVICE_LOST / bindless / sync issues you are hunting) is retained in the log file even when stdout/stderr buffers are lost.
- The log is enabled unconditionally at the very start of `main()`; no new config or command-line flags were added (kept minimal per project rules).
- The messenger is still cleaned up via `vkb::destroy_debug_utils_messenger` in the normal destroy path.
- Optional PBR debug viz remains available by setting `kDebugMode` in `Engine.Render.cpp` (`0` normal, `1` UV, `2` flat by material index). Default is `0`.

Update this file when major phases complete or the focus shifts significantly.
