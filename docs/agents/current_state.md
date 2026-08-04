# Current State

Lightweight snapshot of the AeroBoarEngine project status. Intended to be read quickly by any agent.

## Overall Status

Desktop foundation is solid and past “first triangle.” The engine loads glTF scenes (multi-material, hierarchy, punctual lights), uploads bindless resources, and renders with **GPU frustum + Hi-Z cull**, **mesh-grouped instancing**, and **one multi-draw indirect** call path plus procedural IBL. **Animation:** node TRS, skinned meshes, CPU morph weights. **Physics:** Jolt foundation (step + box bodies); no KHR physics load yet. Scene list in `configuration.json` includes the full glTF-Sample-Assets set for regression browsing. OpenXR / Quest / reverse-Z remain roadmap items. **Next product arc:** ABeautifulGame physics → VR shrink-to-board — see `docs/architecture/vr-chess-physics-plan.md`.

## Major Completed Areas

- Project structure and build system (CMake + FetchContent dependencies)
- Vulkan instance, device, swapchain, and basic pipeline scaffolding
- VMA integration for memory management
- glTF loading via tinygltf (`GltfLoader`) including hierarchy, cameras, `KHR_lights_punctual`
- Resource managers: Mesh, Texture, Material, Scene + shared `gfx::DoubleBufferedBuffer` helper
- Complete bindless GPU upload path at load time (all data visible in shaders)
- Render loop: acquire → frustum cull → depth prepass → HZB build → occlusion cull → shade → present
- Basic PBR forward shader (`pbr.vert` / `pbr.frag`) that samples:
  - Albedo (baseColor) including **alpha** (`baseColorFactor * texture`)
  - Normal map
  - Metallic + Roughness (from metalRoughness texture or factors)
  - Emissive
  - Ambient Occlusion (separate texture)
  - **glTF alphaMode MVP:** OPAQUE / MASK (discard + cutoff) / BLEND (pipeline `SRC_ALPHA` blend); depth prepass skips BLEND and tests MASK. Double-sided via cull-none. **Not yet:** transparent sort, dual opaque/blend pipelines, per-material depth-write off
- Proper vertex attribute input (`gfx::Vertex`, pipeline vertex state, `pbr.vert`) — legacy SSBO vertex pulling is no longer the active path
- Materials SSBO correctly declared as a **single buffer + runtime array** in `pbr.frag` (not a descriptor array); `gfx::Material` is `alignas(16)` / 80-byte stride to match std430
- Scene model: `GameObject` / `RenderMesh` / `TransformManager` (local + parent + dirty-flag `propagate`)
- GPU cull + same-frame Hi-Z + multi-draw indirect (`GpuCulling`, `HzbPyramid`, depth prepass — see tech_context)
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
  - **Y** / **T**: increase / decrease `movement_speed` (keyboard fly only; not mouse sensitivity).
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

**Session snapshot (2026-08-03):** Desktop rendering + animation stack is in good shape for core samples. Next major track is **physics on ABeautifulGame** then **VR chess / scale-down** (plan stored). Advanced glTF material/animation-pointer samples remain incomplete by design.

- Dirty-flag transform `propagate()` landed
- Lighting polish landed: spot packing, dynamic light API, optional HDR equirect IBL
- **glTF animation Phase 1–3 landed:** node TRS + skins + **morph targets** (`MorphSystem`, path `weights`, CPU blend). Multi-clip exclusive play (**N** cycles). Y/T movement speed. Alpha OPAQUE/MASK/BLEND MVP. See `animation-plan.md`.
- **Same-frame occlusion (desktop):** depth prepass → Hi-Z → shade; hysteresis removed. Prepass FS does MASK discard / skips BLEND depth.
- **Physics foundation (landed):** Jolt v5.3.0; `PhysicsWorld`; step + transform sync; optional box demo (`physicsDemo`). See `physics-plan.md`.
- **Extension matrix:** `docs/architecture/gltf-extensions.md` — what we do / don’t support
- **VR chess plan:** `docs/architecture/vr-chess-physics-plan.md`
- **Future tooling:** `glslc` → **glslang** when Quest/Android work starts

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
  Spot packing complete (pos + emission dir + cos cones). Dynamic lights via per-frame upload + `Engine::set_light` / `refresh_lights_from_transforms`. Optional HDR equirect IBL (`environmentHdr` in configuration.json). Still deferred: clustered many-lights, shadows.
- **GPU frustum + same-frame Hi-Z occlusion + instancing + indirect (landed)**:
  - Frame order: frustum cull → **depth-only prepass** (1x, vertex-only) → `HzbPyramid::record_build` → frustum+HZB cull → main shade (MSAA)
  - `gfx::GpuCulling`: `cull_frustum.comp` frustum + optional same-frame Hi-Z; packs visible instances into fixed per-batch regions; `build_indirect.comp` writes draw commands
  - `gfx::HzbPyramid`: half-res min-Z pyramid (double-buffered for frames-in-flight); built from prepass depth at the **current** `view_proj` (no hysteresis)
  - Prepass resources: `renderer.depth_prepass` RP/FBs + `vk.depth_prepass_pipeline`; one depth target per frame-in-flight
  - `hzb_copy` min-downsamples full-res prepass depth into mip0; `hzb_reduce` min-mips
  - Graphics uses binding 1 instance SSBO + **one** `vkCmdDrawIndexedIndirect` per pass (multi-draw; `firstInstance = batch.base`)
  - **Instance index (Vulkan):** VS uses `gl_InstanceIndex` only — it already includes `firstInstance`. Never also add `gl_BaseInstance` or push base (double-count → wrong materials / missing draws).
  - `[Cull]` log from host-visible counts after frame fence (`[hzb=on]` when same-frame path ran)
  - Main-pass MSAA depth resolve may still exist but is **not** the HZB source
- **Depth model:** standard Z today (0=near, 1=far, `LESS`). **Planned:** reverse-Z with VR/multiview depth work (`GREATER`/`GREATER_OR_EQUAL`, clear 0, max-depth Hi-Z) — official roadmap item in `tech_context.md`
- No OpenXR / VR input layer (desktop GLFW only)
- **Scene model (hierarchy + dirty propagate)**: `GameObject` + `RenderMesh` + `TransformManager` with local matrices, parent links, **dirty flags**, and selective `propagate()`. Runtime: `Engine::sync_scene_transforms()` after fence wait. Legacy `SceneInstance` dual-written.
- **glTF animation Phase 1–3 (TRS / skin / morph) yes.** **Not yet:** `KHR_animation_pointer`, `KHR_texture_transform`, advanced material extensions — **AnimationPointerUVs** will not render correctly; see `docs/architecture/gltf-extensions.md`.
- **Physics runtime foundation yes; asset pipeline no.** Jolt + boxes + transform links. **Missing:** KHR physics load, non-box shapes, raycast/impulse API, character controller, ABeautifulGame auto-bodies — `physics-plan.md` + `vr-chess-physics-plan.md`.
- Alpha: MVP only (no transparent sort / dual queues).
- No audio beyond desktop `InputManager` completeness

## Next Immediate Priorities

- [done] Scene light visual correctness: distance-aware auto-exposure + physical inverse-square; load-time light logging
- [done] Proper lighting layout: `FrameConstants` UBO + lights SSBO (textures moved to binding 7)
- [done] Lighting polish: spot direction packing, dynamic light API, HDR equirect IBL
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
- [done] Dirty-flag propagate + per-frame `sync_scene_transforms` (FIF-safe GPU cull model update)
- [done] Occlusion culling / Hi-Z: same-frame depth prepass → pyramid → cull (hysteresis removed)
- [done] Single multi-draw indirect: `build_indirect` writes `firstInstance = batch.base`; `pbr.vert` uses `gl_InstanceIndex` only (includes base on Vulkan); one `vkCmdDrawIndexedIndirect` for all batches
- [done] glTF node TRS animation Phase 1 (`AnimationSystem`, TRS locals, auto-play)
- [done] Skinned animation Phase 2 (`SkinSystem`, JOINTS/WEIGHTS, palette, VS skin)
- [done] Morph targets Phase 3 CPU (`MorphSystem`, AnimatedMorphCube)
- [done] Physics foundation (Jolt `PhysicsWorld`, step/sync, floor+box demo)
- [done] Richer skinned demos (CesiumMan, Fox multi-clip, mesh-relative skin, non-indexed meshes)
- [done] Alpha mode MVP (OPAQUE / MASK / BLEND) + double-sided cull-none
- [done] Full sample-assets list in `configuration.json` for manual regression
- [done] Docs: extension matrix, VR chess plan, session progress
- **Next immediate:** **ABeautifulGame auto-box physics + desktop knock-over** (`vr-chess-physics-plan.md` Phase A) → KHR physics → player scale → OpenXR
- **Roadmap (extensions):** `KHR_texture_transform` + `KHR_animation_pointer` (AnimationPointerUVs); advanced materials — `gltf-extensions.md`
- **Roadmap (VR / Quest):** reverse-Z + multiview + per-eye HZB; OpenXR input; VR chess demo
- **Roadmap (physics):** shapes API + KHR load + character controller — `physics-plan.md`
- Future tooling: glslang when Quest/Android — keep `glslc` until then
- [done] Desktop input layer: `core::InputManager` + `scene::Camera` decoupling; **Y/T** keyboard move speed
- glTF loader robustness: `extract_mesh_data` now accepts primitives that provide only POSITION (common in minimal test assets). Missing NORMAL defaults to (0,0,1); missing TEXCOORD_0 defaults to (0,0). This allows the Cameras.gltf pure-camera test scene (and similar) to load and render its proxy geometry. Also injects a default white material when the glTF contains no materials array (primitives may still reference default material via -1).
- Node transform extraction (`extract_node_transform`): now correctly defaults absent translation (0,0,0), rotation (identity quat), and scale (1,1,1) per glTF spec. Previous `value_or_ident` always supplied 1.0 which placed nodes incorrectly for assets like Cameras.gltf that omit TRS keys on some nodes (e.g. camera nodes with only translation, mesh nodes with only rotation). The helper was removed as dead after the fix. Quat component order also corrected for glTF [x,y,z,w] layout.

## GPU Upload Path (Completed)
The one-time scene upload at load is now fully wired:
- Bindless descriptor set is allocated with correct variable-count + update-after-bind flags.
- Layout declares the global tables (see tech_context.md for the exact binding numbers).
- Double-buffered managers (Scene, Material, Mesh via `gfx::DoubleBufferedBuffer`) flip + bind after GltfLoader populates CPU side.
- Textures create images + upload via transfer queue + bind into the array.

Dynamic transforms: dirty flags + fence-gated per-frame cull model upload (`sync_scene_transforms`). Lights re-uploaded every frame.

Per-frame bindless descriptor sets (one per `MAX_FRAMES_IN_FLIGHT`) cover all bindings including binding 0 (`FrameConstants` UBO). Static resources are bound to all sets at load; per-frame data is updated on the matching set for `current_frame` (`bind_frame_lighting_to_all_sets` / `write_frame_lighting`).

## Rendering Milestone (Achieved)

Active path:

- `pbr.vert` / `pbr.frag` — bindless PBR (albedo, normal, metal/rough, emissive, AO)
- Material index from **instance SSBO** (`DrawInstanceGPU.meta.x`), not push constants
- Push constants: `viewProj` (+ reserved `extra`)
- Multi-material scenes: materials as elements of a **single** SSBO (see below)
- Frustum → depth prepass → Hi-Z → occlusion cull → shade (multi-draw indirect)

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
