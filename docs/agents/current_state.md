# Current State

Lightweight snapshot of the AeroBoarEngine project status. Intended to be read quickly by any agent.

## Overall Status

Desktop foundation is solid. The engine loads glTF scenes (multi-material, hierarchy, punctual lights), uploads bindless resources, and renders with **GPU frustum cull** (always), **optional conservative Hi-Z** (`occlusionCull`, off by default / off on Adreno), **mesh-grouped instancing**, and **one multi-draw indirect** call path plus procedural IBL. **Animation:** node TRS, GPU skin palettes (`skin_palette.comp` from `worlds[]`), CPU morph weights. **ECS Phase 1–3:** World, Player, `InputFrame`, **FpsMove** on any authored body + EditorHotkeys, glTF `extras.ECS_Components_v1`. **Third-person boom:** WASD moves the extras `player` body; camera follows; `boom_offset` is **asset meters × `worldScale`**. **Physics:** Jolt + KHR MVP, LinearCast CCD, **`worldScale`** (small assets e.g. chess `10`), **debug draw** (F3), **kill floor**; player-subtree hulls kinematic. Player / loco / physics paths are **data-driven** (extras + KHR), not scene-name hardcoded. Current default test scene is **ABeautifulGameGame**. Scene list includes the full glTF-Sample-Assets set. OpenXR / Quest remain roadmap; **reverse-Z is on desktop**. **Next product focus:** locomotion authoring (in-place / scaled hip translations, or later IK) or VR path — `animation-plan.md` §9 / `ecs-plan.md`. **Investigate:** Tiled Clustered Forward (**TCF**) for mobile/Quest lighting — `lighting-implementation.md` §4.

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
  - **glTF alphaMode:** OPAQUE / MASK (discard + cutoff) / BLEND + transmission → **dual shade pipelines** (opaque depth-write on; transparent blend + depth-write off). Depth/Hi-Z prepass emits **opaque writers only**. **CPU sort + WBOIT** (McGuire); fallback is traditional sorted blend.
  - Multi-UV + static `KHR_texture_transform`; UV0/UV1 as **half floats** (`R16G16B16A16_SFLOAT`); AO multiplies **indirect only** (not emissive/direct)
- Proper vertex attribute input (`gfx::Vertex`, pipeline vertex state, `pbr.vert`) — legacy SSBO vertex pulling is no longer the active path
- Materials SSBO: single buffer + runtime array in `pbr.frag`; `gfx::Material` is `alignas(16)` / **256-byte** stride (maps, multi-UV, clearcoat/transmission/iridescence factors)
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
  - Remote/HIDDEN: `WM_INPUT` look, **no** `ClipCursor`. Warp only at the desktop rail if the OS honors it. Absolute raw baseline is kept across warps.
  - **Accepted:** RDP + trackpad look can still peg at the desktop/screen rail (`SetCursorPos` ignored). Parked until **camera cleanup**. Details: `docs/architecture/desktop-inputs.md` § Accepted look-rail limit.
  - Decouples raw input from `scene::Camera` (Camera now receives an `InputManager&` and queries processed deltas/keys).
  - See `docs/architecture/desktop-inputs.md` for full design rationale.
- `AGENTS.md` and shared documentation in `docs/agents/`

## Current Focus Areas

**Near-term priority:** polish locomotion (in-place / scaled hip translations, hysteresis / jump clips, or later IK) or VR path. **Phase 4 crossfade + Idle/Walk/Run** landed; clip names come from extras (`idle` / `walk` / `run`). Addon: `tools/blender/src/animation_transfer/` (any matching armatures). See `animation-plan.md` §9.5.

- **ECS Phase 1–3 + FpsMove:** any live player **body** uses grounded WASD + boom/FPS cam; free-fly `DesktopMove` only when there is no `TransformLink`. **Third-person** extras `"camera": "third_person"` / `boom_offset` (asset meters × `worldScale`). GPU `worlds[]` tracks `TransformManager::world_serial()` so skinning follows WASD after an early propagate.
- Desktop rendering + animation solid (TRS / skin / morph, Hi-Z, opaque/transparent; CarConcept usable)
- **Physics:** knock-over + `worldScale` + KHR + kill floor + debug draw. Current tabletop test: ABeautifulGameGame at `worldScale: 10`
- **Extension matrix:** `docs/architecture/gltf-extensions.md`
- **Blender addons:** `tools/blender/src/ecs_components_editor/` (extras UI; human-scale / small-asset presets); `tools/blender/src/animation_transfer/` (copy clips between matching armatures, scale location to dest size)
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
  Spot packing complete (pos + emission dir + cos cones). Dynamic lights via per-frame upload + `Engine::set_light` / `refresh_lights_from_transforms`. Optional HDR equirect IBL (`environmentHdr` in configuration.json). Still deferred: **TCF / clustered many-lights** (investigate for Quest — `lighting-implementation.md` §4), shadows.
- **GPU frustum (always) + optional conservative Hi-Z** (`occlusionCull`, default **false**; **off on Adreno**). RG min/max pyramid — `visibility-lod-plan.md`.
- **Distance LOD (design):** **CascadeBake** / `CascadeOven` — meshlets → octahedral impostors → static skybox bake — `cascadebake-plan.md`. **Not implemented.**
- **GPU frustum + same-frame Hi-Z + opaque/transparent shade (landed)**:
  - Frame order (compute **outside** render passes): **one** frustum cull when Hi-Z is off; with Hi-Z: frustum → depth prepass → HZB → frustum+HZB shade cull. **WBOIT transparents are GPU-emitted** (CPU sort only if WBOIT init failed).
  - `gfx::GpuCulling`: packed `worlds[]` SSBO (one mat4 per transform); GpuCullItem is 64 B (no model). Instance + indirect buffers are **GpuOnly** (unmapped) when WBOIT is on — Adreno/UMA, no staging copy. Transparent instance half allocated only if the scene has blend/transmission.
  - `gfx::HzbPyramid`: half-res min-Z from **opaque-only** prepass (glass must not seal cabin for occlusion)
  - Graphics: binding 1 combined instance SSBO; **one** `vkCmdDrawIndexedIndirect` per shade pass; transparent uses `vk.transparent_pipeline` (blend, depth write off)
  - **Instance index (Vulkan):** VS uses `gl_InstanceIndex` only (includes `firstInstance`)
  - `[Cull]` log from host-visible counts after frame fence (`[hzb=on]` when same-frame path ran)
- **Depth model:** **reverse-Z** (1=near, 0=far, `GREATER`, clear 0). Hi-Z conservative test uses min (far/hole). Multiview HZB still later — `tech_context.md`
- No OpenXR / VR input layer (desktop GLFW only)
- **Desktop look rail (accepted):** remote/HIDDEN + trackpad can still stop at the desktop/screen edge. Not chasing another warp/`ClipCursor` pass. **Reopen at camera cleanup** — `desktop-inputs.md` § Accepted look-rail limit. Local `DISABLED` + USB mouse unchanged.
- **Player locomotion:** any authored player **body** (`TransformLink`) uses **FpsMove** — WASD moves the character, not the camera. **Third-person boom** when extras `"camera": "third_person"` / `boom_offset` (**asset meters × `worldScale`**). GPU `worlds[]` follows `TransformManager::world_serial()` so a WASD root still skins after an early propagate. **`LocomotionAnim`** (extras `locomotion_anim` / `locomation_anim`) crossfades Idle/`T-Pose` / Walk / Run from `horizontal_speed`. Clips play as authored (no post-sample hips clamp / foot plant). Feet through the floor is retarget/IK — `animation-plan.md` §9.1. **N** crossfades clips (0.2 s). **Free-fly `DesktopMove`** only for the inspector player (no body). Player-subtree KHR hulls are forced **kinematic**.
- **Scene model:** `GameObject` + `RenderMesh` + `TransformManager` (local + parent + dirty `propagate`); dual-write `SceneInstance`; ECS dual-write. Full GameObject→Entity render migration later
- **glTF animation Phase 1–3 yes.** Skin palettes are a **GPU compute** pass from `worlds[]` (`skin_palette.comp`). Static `KHR_texture_transform` yes. **Not yet:** `KHR_animation_pointer`, GPU morph, full material set — see `gltf-extensions.md`
- **Physics:** Jolt + KHR MVP, CCD, `worldScale`, debug lines. **Missing:** compound/triangle mesh colliders, raycast/impulse tools, proper character controller — `physics-plan.md`
- Alpha/transmission: dual pipelines yes; **CPU back-to-front sort** + **weighted blended OIT** (McGuire). Transparents test opaque depth (no second depth buffer). Sort still used (WBOIT is approximate).
- **Volume / dispersion / thick glass:** deferred. Thin-wall transmission MVP only. Future option: parse factors + desktop-only refraction path, **off on Quest** — see `gltf-extensions.md` §2.2. Showcase: `DragonDispersion` not reference-correct.
- No audio beyond desktop `InputManager` completeness

## Next Immediate Priorities

- [done] Scene light visual correctness: distance-aware auto-exposure + physical inverse-square; load-time light logging
- [done] Proper lighting layout: `FrameConstants` UBO + lights SSBO (textures moved to binding 7)
- [done] Lighting polish: spot direction packing, dynamic light API, HDR equirect IBL
- [done] Production IBL (procedural env + SH + prefiltered cube + BRDF LUT)
- [done] Switch from raw SSBO vertex pulling to proper vertex attribute input
- [done] Materials SSBO GLSL shape + C++ stride (now **256-byte** Material)
- [done] PBR path hygiene
- [done] Multi-UV + static `KHR_texture_transform`; half-float vertex UVs
- [done] Material MVP: clearcoat / emissive_strength / transmission / iridescence factors
- [done] Opaque + transparent shade split (Hi-Z opaque-only; glass depth-write off)
- [done] AO on indirect only; combined cull instance SSBO (no mid-CB descriptor flip)
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
- [done] KHR physics load MVP + capsule/box/convex hull; LinearCast CCD; **`worldScale`**; physics debug draw (F3)
- [done] ECS Phase 1–3 (World, Player, InputFrame, systems, extras, scripts)
- [done] ABeautifulGameScene desktop knock-over (capsule vs pieces; tabletop `worldScale: 10`)
- [done] Richer skinned demos (CesiumMan, Fox multi-clip, mesh-relative skin, non-indexed meshes)
- [done] Alpha mode OPAQUE / MASK / BLEND + double-sided cull-none + dual shade pipelines
- [done] Full sample-assets list in `configuration.json` for manual regression
- [done] Docs: extension matrix, VR chess plan, ECS plan
- [done] Third-person follow boom (`CameraRig.third_person` / extras `boom_offset`)
- [done] Animation crossfade + `LocomotionAnim` Idle/Walk/Run (extras clip names)
- [done] GPU skin palettes (`skin_palette.comp`) + `worlds[]` upload via `world_serial` (WASD moves the skinned mesh)
- [done] Authored body always `FpsMove`; `boom_offset` asset meters × `worldScale`; `ECS_Components_v1` wins over Blender settings
- [done] Player-subtree KHR hulls forced kinematic
- [done] Reverted post-sample foot plant / hips Y clamp (was splitting body vs legs). Next: author in-place / scaled hip translations, or IK.
- [done] Engine + Blender addons generalized: no scene-name hardcoding; `CameraRig` defaults human-scale; extras/KHR data-driven
- **Next immediate:** locomotion polish (hysteresis / jump clips) or VR path
- **Investigate (lighting / Quest):** Tiled Clustered Forward (**TCF**) instead of looping all lights in the forward shader — `lighting-implementation.md` §4. Do not start this until scoped; Adreno TBDR / GMEM constraints apply.
- **Then:** ECS Phase 4 events/timers or Jolt sensors; GameObject→Entity render migration
- **When cameras are cleaned up:** reopen captured look-rail (`desktop-inputs.md`); do not spend an input-only pass before that
- **Roadmap (extensions):** `KHR_animation_pointer`; unlit; variants UI; optional desktop-only volume/dispersion — `gltf-extensions.md`
- **Roadmap (VR / Quest):** reverse-Z + multiview HZB; OpenXR; shrink-to-board VR chess — `vr-chess-physics-plan.md`
- **Roadmap (physics):** mesh colliders, filters, character controller polish — `physics-plan.md`
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
- Frustum → depth prepass → Hi-Z → dual cull → opaque then transparent shade (multi-draw indirect)

### Materials SSBO (important)

C++ binds **one** `STORAGE_BUFFER` for all materials (binding 2). The shader must declare a runtime array **inside** that buffer:

```glsl
struct Material { /* matches gfx::Material, 256-byte std430 stride */ };
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
