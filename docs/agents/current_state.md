# Current State

Lightweight snapshot of the AeroBoarEngine project status. Intended to be read quickly by any agent.

## Overall Status

Desktop foundation is solid and past “first triangle.” The engine loads glTF scenes (multi-material, hierarchy, punctual lights), uploads bindless resources, and renders with **GPU frustum + Hi-Z cull**, **mesh-grouped instancing**, and **one multi-draw indirect** call path plus procedural IBL. Default scene selection is via `assets/scenes/configuration.json` (often multi-material assets such as ABeautifulGame). OpenXR / Quest / reverse-Z remain roadmap items.

## Major Completed Areas

- Project structure and build system (CMake + FetchContent dependencies)
- Vulkan instance, device, swapchain, and basic pipeline scaffolding
- VMA integration for memory management
- glTF loading via tinygltf (`GltfLoader`) including hierarchy, cameras, `KHR_lights_punctual`
- Resource managers: Mesh, Texture, Material, Scene + shared `gfx::DoubleBufferedBuffer` helper
- Complete bindless GPU upload path at load time (all data visible in shaders)
- Render loop: acquire → GPU cull/HZB build → bindless draw → present
- Basic PBR forward shader (`pbr.vert` / `pbr.frag`) that samples:
  - Albedo (baseColor)
  - Normal map
  - Metallic + Roughness (from metalRoughness texture or factors)
  - Emissive
  - Ambient Occlusion (separate texture)
- Proper vertex attribute input (`gfx::Vertex`, pipeline vertex state, `pbr.vert`) — legacy SSBO vertex pulling is no longer the active path
- Materials SSBO correctly declared as a **single buffer + runtime array** in `pbr.frag` (not a descriptor array); `gfx::Material` is `alignas(16)` / 80-byte stride to match std430
- Scene model: `GameObject` / `RenderMesh` / `TransformManager` (local + parent + `propagate()` at load)
- GPU cull + Hi-Z + multi-draw indirect (`GpuCulling`, `HzbPyramid`; desktop HZB hysteresis — see tech_context)
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

- Dirty-flag per-frame `propagate()` when animated transforms land
- Spot light direction packing, dynamic lights, HDR env loading
- **Future tooling:** migrate shader compile from `glslc` → **glslang** when cross-platform (Quest/Android) work starts — see `tech_context.md`
- **VR depth / cull quality (roadmap):** reverse-Z + replace HZB hysteresis with same-frame / reprojected Hi-Z — see `tech_context.md`
- **Physics (roadmap):** Jolt runtime + **glTF Khronos physics extensions** for model physics properties (`KHR_physics_rigid_bodies`, `KHR_implicit_shapes`) — see `tech_context.md` § Physics assets

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
- **GPU frustum + Hi-Z occlusion cull + instancing + indirect (landed)**:
  - `gfx::GpuCulling`: `cull_frustum.comp` frustum + previous-frame Hi-Z test; packs visible instances into fixed per-batch regions; `build_indirect.comp` writes draw commands
  - `gfx::HzbPyramid`: half-res min-Z pyramid (double-buffered); built after the main pass from resolved depth
  - MSAA path uses **depth stencil resolve** (attachment 3) so single-sample depth is available for HZB; MSAA depth stays transient/`DONT_CARE`
  - Compute runs before the render pass; graphics uses binding 1 instance SSBO + **one** `vkCmdDrawIndexedIndirect` (multi-draw; `firstInstance = batch.base`)
  - **Instance index (Vulkan):** VS uses `gl_InstanceIndex` only — it already includes `firstInstance`. Never also add `gl_BaseInstance` or push base (double-count → wrong materials / missing draws).
  - `[Cull]` log from host-visible counts after frame fence
  - HZB occlusion uses **desktop hysteresis** (see `tech_context.md` § Hi-Z occlusion hysteresis): off on any camera motion; on after ~24 still frames + capture match + bias warmup; far objects largely exempt from hard HZB cull. Log tag `[hzb=on|off]`. **Interim only** for VR (same-frame / reprojected HZB + reverse-Z).
  - Still TODO for cull quality: same-frame two-phase occlusion / reprojected HZB while the camera (or HMD) moves
- **Depth model:** standard Z today (0=near, 1=far, `LESS`). **Planned:** reverse-Z with VR/multiview depth work (`GREATER`/`GREATER_OR_EQUAL`, clear 0, max-depth Hi-Z) — official roadmap item in `tech_context.md`
- No OpenXR / VR input layer (desktop GLFW only)
- **Scene model (hierarchy landed)**: `GameObject` + `RenderMesh` + `TransformManager` with **local matrices + parent links + `propagate()`** at load. glTF load walks the node tree (`set_local` + `set_parent`), then `propagate()`, then `refresh_instance_worlds()` + `GpuCulling::build_scene` (world matrices). Legacy `SceneInstance` dual-written and re-synced after propagate.
- **No physics yet** (runtime or asset). **Planned:** Jolt for simulation; physics properties on models via **Khronos glTF extensions** (`KHR_physics_rigid_bodies`, `KHR_implicit_shapes` and related as ratified) loaded through the glTF pipeline — see `docs/agents/tech_context.md` § Physics assets and `docs/project-plan.md`.
- No audio, or higher-level input abstraction beyond the desktop layer (desktop input is complete via `core::InputManager`)

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
- [done] Frustum culling + multi-draw indirect
- [done] GPU frustum cull compute (`GpuCulling`)
- [done] Transform hierarchy: local matrix + parent + `propagate()` at load; dual-write refresh + GPU cull after propagate
- [done] Occlusion culling / Hi-Z: depth resolve + `HzbPyramid` + cull shader test (previous-frame, double-buffered; desktop hysteresis documented)
- [done] Single multi-draw indirect: `build_indirect` writes `firstInstance = batch.base`; `pbr.vert` uses `gl_InstanceIndex` only (includes base on Vulkan); one `vkCmdDrawIndexedIndirect` for all batches
- **Next immediate:** dirty-flag per-frame `propagate()` when animated transforms land; lighting polish (spot packing, HDR env)
- **Roadmap (VR / Quest depth phase):** reverse-Z; same-frame or reprojected Hi-Z (drop desktop hysteresis); multiview stereo depth — see `tech_context.md` § Depth buffer model
- **Roadmap (physics):** Jolt integration + load physics from glTF Khronos extensions (`KHR_physics_rigid_bodies`, `KHR_implicit_shapes`) — see `tech_context.md` § Physics assets
- Future tooling: glslang shader toolchain when cross-platform (Quest/Android) work starts — keep `glslc` until then (see `tech_context.md`)
- [done] Desktop input layer: `core::InputManager` (callback-driven deltas + EWMA + acceleration + capture state) + full decoupling from `scene::Camera` (see `docs/architecture/desktop-inputs.md` and the implementation plan). Pitch sign convention restored to original comfortable default.
- glTF loader robustness: `extract_mesh_data` now accepts primitives that provide only POSITION (common in minimal test assets). Missing NORMAL defaults to (0,0,1); missing TEXCOORD_0 defaults to (0,0). This allows the Cameras.gltf pure-camera test scene (and similar) to load and render its proxy geometry. Also injects a default white material when the glTF contains no materials array (primitives may still reference default material via -1).
- Node transform extraction (`extract_node_transform`): now correctly defaults absent translation (0,0,0), rotation (identity quat), and scale (1,1,1) per glTF spec. Previous `value_or_ident` always supplied 1.0 which placed nodes incorrectly for assets like Cameras.gltf that omit TRS keys on some nodes (e.g. camera nodes with only translation, mesh nodes with only rotation). The helper was removed as dead after the fix. Quat component order also corrected for glTF [x,y,z,w] layout.

## GPU Upload Path (Completed)
The one-time scene upload at load is now fully wired:
- Bindless descriptor set is allocated with correct variable-count + update-after-bind flags.
- Layout declares the global tables (see tech_context.md for the exact binding numbers).
- Double-buffered managers (Scene, Material, Mesh via `gfx::DoubleBufferedBuffer`) flip + bind after GltfLoader populates CPU side.
- Textures create images + upload via transfer queue + bind into the array.

Future dynamic updates will need per-frame-in-flight fencing + dirty tracking (transforms, lights).

Per-frame bindless descriptor sets (one per `MAX_FRAMES_IN_FLIGHT`) cover all bindings including binding 0 (`FrameConstants` UBO). Static resources are bound to all sets at load; per-frame data is updated on the matching set for `current_frame` (`bind_frame_lighting_to_all_sets` / `write_frame_lighting`).

## Rendering Milestone (Achieved)

Active path:

- `pbr.vert` / `pbr.frag` — bindless PBR (albedo, normal, metal/rough, emissive, AO)
- Material index from **instance SSBO** (`DrawInstanceGPU.meta.x`), not push constants
- Push constants: `viewProj` (+ reserved `extra`)
- Multi-material scenes: materials as elements of a **single** SSBO (see below)
- GPU frustum + Hi-Z cull → one `vkCmdDrawIndexedIndirect` multi-draw

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
- [done] Shared double-buffer helper: `gfx::DoubleBufferedBuffer` used by `MaterialManager`, `SceneManager`, and `MeshManager` (upload/render toggle, growth, memcpy, bind).
- TextureManager remains image-oriented (not the same buffer pair pattern).
- Standardize cache / storage naming across the resource managers (currently a mix of `cpu_materials`, `mesh_cache`, `texture_cache`, etc.).
- [done] Replace the raw `#define INVALID_HANDLE` in `core/Handle.h` with a `constexpr` constant.
- Reduce unnecessary `gfx::` qualification on types when already inside `namespace gfx`.
- Consider whether the top-level `AeroBoar` stub class is still needed.
- [done] Complete per-frame bindless descriptor consistency for globals (binding 0): legacy `globals_upload`/`globals_render` removed, `bind_frame_globals_to_all_sets()` helper added, all sets now correctly paired with their buffers.
- [done] PBR diagnostic noise and accidental `extra.y` debug-mode pollution cleaned up (2026-08).
- [done] Removed unused `debug_draw.*` / `screen_clear.*` shaders; dead MeshManager first-mesh helpers; unused `batch_bases` after multi-draw; PBR push-constant debug viz branches.

## Debugging & Diagnostics
- `core/Log.h` now supports a persistent log file (`aero_boar.log`). The file is explicitly opened relative to `std::filesystem::current_path()` (the process working directory) at startup. This respects the project's CMake setting `VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"`, so under normal debugging the log lands in the `build/` folder. At init time the code prints the **full absolute path** to both the console and the Visual Studio / VS Code debugger Output window so you always know exactly where the validation crash log went.
- Replaced vk-bootstrap's default debug messenger with a custom `VkDebugUtilsMessengerEXT` callback (`vulkan_debug_callback` in Engine.InitializeDevices.cpp) that funnels every validation layer message (errors, warnings, info, loader messages, etc.) through the logger. This ensures the exact validation text that precedes a crash (e.g. the DEVICE_LOST / bindless / sync issues you are hunting) is retained in the log file even when stdout/stderr buffers are lost.
- The log is enabled unconditionally at the very start of `main()`; no new config or command-line flags were added (kept minimal per project rules).
- The messenger is still cleaned up via `vkb::destroy_debug_utils_messenger` in the normal destroy path.
Update this file when major phases complete or the focus shifts significantly.
