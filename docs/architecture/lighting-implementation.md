# Lighting Implementation Guide

**Current Lighting Model**:  
Scene-driven lights via glTF `KHR_lights_punctual` (directional/point/spot, world transforms from node hierarchy) + correct GGX BRDF (via temporary `FrameGlobals` UBO at binding 0). Engine global directional is the fallback when no scene lights are present. Textured IBL etc. still deferred.

**Status of current implementation**: The `FrameGlobals` UBO + `gfx::FrameGlobals` / `gfx::Light` structs, fixed `MAX_LIGHTS` arrays, per-frame direct mapped writes, and any std140 padding accommodations (oversized `padding0`/`padding1` arrays in C++ to match GLSL uniform block layout rules for scalar arrays) is **temporary scaffolding**. It was built to quickly enable scene lights + GGX during early development. It has known layout, update, and scalability issues.

**Next step**: The explicit next phase of the project is to implement a **proper lighting solution**. Recommended direction (based on recent analysis of the std140 pain and double-buffering limitations):
- Small, clean per-frame constants UBO (camera, exposure, light count, IBL indices, flags).
- Dedicated lights buffer (SSBO preferred for `std430` / flexible arrays / no padding hacks, easy dynamic updates, and larger light counts).
- Proper support for dynamic lights, many lights (clustered/tiled later), full production IBL, and integration with the future scene/GameObject model.
- See "Phased Roadmap" and "Known Rough Edges" below for context.

**Long-term Target**: High-quality, Quest 3 friendly PBR with IBL and efficient multi-light support.

**See also**:
- `docs/agents/current_state.md` (current gaps & priorities)
- `docs/architecture/pipeline-implementation.md` (subpass & overall pipeline vision)
- `docs/agents/tech_context.md` (descriptor bindings, per-frame globals)
- `docs/agents/architecture_principles.md` (Quest 3 TBDR rules)

## 1. Current State (Simplified Global Light Model)

The renderer uses a single forward pass with `pbr.vert` + `pbr.frag`.

### Current Model (What Ships Today)
- **One canonical global directional light** owned by the engine (`renderer.globalLight`).
- This light is always present and is written as light 0 in the `FrameGlobals` UBO (binding 0).
- Correct Cook-Torrance GGX BRDF with proper Fresnel, distribution, and geometry terms.
- Basic diffuse spherical harmonic (SH) ambient contribution (very simple L0 fallback today).
- Camera position is correctly provided for view-dependent lighting.
- glTF `KHR_lights_punctual` extraction + world-transform application during load is now the active source for scenes that contain lights (e.g. DamagedHelmetScene). Engine global is fallback only.
- Full multi-light (many lights, clustering), dynamic lights, shadows, and textured IBL (prefiltered cubemaps + BRDF LUT) are explicitly future work.

This model is intentionally simple and reliable while the rest of the engine (scene model, culling, VR, etc.) matures.

### 1.1 Historical Context (Pre-Current Model)

### 1.1 Pre-Phase 1 Lighting (original "Phase 0")

```glsl
// shaders/pbr.frag (before this increment)
const vec3  LIGHT_DIR   = normalize(vec3(0.0, -1.0, 0.0));
const vec3  LIGHT_COLOR = vec3(1.0, 0.98, 0.95);
const float LIGHT_INTENSITY = 1.0;
const vec3  AMBIENT       = vec3(0.03);

... later ...
vec3 diffuse  = albedo.rgb * (1.0 - sampledMetal) * NdotL;
vec3 specular = vec3(pow(NdotH, mix(2.0, 64.0, 1.0 - sampledRough))) * sampledMetal * NdotL;
vec3 color = (diffuse + specular) * LIGHT_COLOR * LIGHT_INTENSITY;
color += albedo.rgb * AMBIENT;
```

**Problems**:
- Completely fake specular (no Fresnel, no distribution, no geometry term).
- No energy conservation.
- Fixed single directional light only.
- Viewer assumed at origin (`V = normalize(-inWorldPos)`).
- Constant ambient only (no environment).
- Roughness response was poor on real assets.

### 1.2 Phase 1 Changes (implemented)

- Replaced the lighting block with a correct Cook-Torrance microfacet BRDF using GGX.
- Added standard helper functions: `D_GGX`, `F_Schlick`, `G_SmithGGX`.
- Proper F0 handling (0.04 for dielectrics, albedo for metals).
- Diffuse is now `(1-F) * (1-metal) * albedo / PI * NdotL`.
- Specular uses the full D·F·G / (4·N·V·N·L) formulation.
- Slightly improved ambient (tinted constant as a bridge to real IBL).
- All existing paths (normal mapping with flag-controlled flip, emissive, separate AO, ORM sampling, debug modes) are unchanged.

The visual result on metallic/rough surfaces is dramatically better (correct edge highlights, plausible roughness variation, energy-conserving response).

The math is now production-ready for analytic lights; only the light source data and IBL are still minimal.

## 2. Limitations & Quest 3 Constraints

- **Limited lights per scene** (capped at MAX_LIGHTS=8 in UBO; fine for typical authored content).
- No dynamic (per-frame) mutation of scene light properties/positions yet.
- No image-based lighting (reflections, diffuse environment beyond the simple SH fallback).
- glTF `KHR_lights_punctual` lights are now fully supported (extraction + node transform application).
- No shadows (see `PassType::Shadow` scaffolding).
- Spot light cone math uses a temporary packing hack (see Known Rough Edges).

**Quest 3 (Adreno 740 / TBDR) realities** (from `architecture_principles.md`):
- Every texture sample and DRAM round-trip is expensive.
- Prefer precomputed data (SH coefficients, prefiltered cubemaps with few mips).
- Keep lighting calculations in the same subpass/tile as much as possible.
- Non-uniform indexing and heavy per-pixel branching hurt.

## 3. Design Principles

1. **Reuse existing infrastructure**:
   - Double-buffered SSBO/UBO managers (`gfx/MaterialManager.*` is the canonical pattern).
   - Bindless texture array for any IBL cubemaps/LUTs.
   - Binding 0 in the global descriptor set is explicitly reserved for "Per-frame globals (future)" (`docs/agents/tech_context.md`).

2. **Data-oriented & cache-friendly**:
   - Small, plain structs for lights (similar to `gfx::Material`).
   - Prefer SOA or small fixed arrays for Phase 1–2; grow into a proper `LightManager` only when needed.

3. **Quest 3 friendly IBL**:
   - Diffuse: 3-band spherical harmonics (9 coefficients) or a very low-res cubemap.
   - Specular: prefiltered cubemap (GGX importance sampled) + 2D BRDF LUT (64×64 is enough).
   - Sample count kept very low (1–4 taps for specular).

4. **Pipeline evolution**:
   - Phase 1–2 stay in the current single forward subpass.
   - Later lighting can move to a dedicated lighting subpass (reading G-buffer or using input attachments) per the target in `pipeline-implementation.md`.

5. **glTF compatibility**:
   - Support `KHR_lights_punctual` for punctual lights (directional, point, spot).
   - IBL is an engine-level asset (not part of the glTF scene for now).

## 4. Phased Roadmap

### Phase 0 (Original)
Hardcoded single directional + constant ambient + fake specular (documented above).

### Phase 1 — Analytic PBR + Multi-Light Foundation (Completed in prior increment)
- Proper GGX BRDF implemented.
- Two analytic lights + real camera position (via extended push constants).

### Phase 2 — Data-Driven Lighting Foundation + glTF Lights (Temporary Scaffolding, Completed)
We built the initial infrastructure (FrameGlobals UBO at binding 0, `gfx::Light` + `FrameGlobals` layouts with std140 accommodations, `GltfLoader::extract_light_data`, world-transform application for light nodes during traversal using the node's final composed matrix, double-buffered upload, proper GGX + multi-light loop in shader).

- When a glTF scene contains `KHR_lights_punctual` entries (and nodes referencing them), the lights are extracted, transformed into world space using their node hierarchy, and become the active lights written to the (temporary) UBO (up to MAX_LIGHTS=8).
- Engine `globalLight` is only used as fallback for scenes with no lights (and supports live tweak when active).
- All three punctual types are supported in data; directional and point are fully wired in shader; spot has the known packing limitation noted in "Known Rough Edges".
- Camera position, exposure, and basic SH ambient are provided every frame.

**This Phase 2 implementation is temporary**. The single `FrameGlobals` UBO, fixed arrays, direct per-frame writes to mapped memory, incomplete globals double-buffering, and C++/GLSL layout padding workarounds (required because `uint padding0[2]` arrays in a `uniform` block force 16-byte stride under std140) were sufficient to get scene lights working for iteration and debugging. They are not the intended long-term design.

**Current active behavior (temporary)**: Scene `KHR_lights_punctual` lights (or engine global fallback) + correct GGX BRDF via the FrameGlobals UBO. This enables development but will be replaced by the proper lighting solution described at the top of this document.

### Phase 3 — IBL (Production Look)
Basic diffuse SH coefficients are populated and evaluated in the shader as a simple ambient contribution.

Full production IBL (real prefiltered specular cubemaps, BRDF LUT, proper asset pipeline, and textured environment) is deferred along with the rest of advanced lighting work. The UBO fields and shader hooks exist so we can pick this up cleanly later.

### Phase 3 — IBL (Production Look)
Basic diffuse SH coefficients are populated and evaluated in the shader as a simple ambient contribution.

Full production IBL (real prefiltered specular cubemaps, BRDF LUT, proper asset pipeline, and textured environment) is deferred along with the rest of advanced lighting work. The UBO fields and shader hooks exist so we can pick this up cleanly later.

### Phase 4 — Advanced / VR
- Many lights via clustered/tiled forward or light grid compute.
- Shadows (tying into existing `PassType::Shadow` and future subpasses).
- Screen-space reflections or cheap GI approximations suitable for TBDR.
- Full multiview + foveated considerations.

Each phase must document:
- New or changed descriptor bindings.
- CPU data structures and upload path.
- Shader impact (sample count, branching).
- Performance notes for Quest 3.

## 5. Data & Resource Design

### 5.1 Per-Frame Globals (Binding 0)
Activate the already-declared binding 0 as a small uniform buffer:

```glsl
layout(set = 0, binding = 0) uniform FrameGlobals {
    vec3  cameraPosition;
    float exposure;
    // Lights (Phase 1–2)
    uint32_t  lightCount;
    vec4  lightDirections[4];   // direction.xyz + unused
    vec4  lightColors[4];       // rgb + intensity
    // IBL (Phase 3+)
    vec3  shCoefficients[9];    // or indices into bindless for cubemaps
    // ...
} globals;
```

This matches the "Per-frame globals (future)" reservation exactly.

### 5.2 Light Representation (CPU)
Follow `gfx::Material` style (plain struct, 16-byte aligned, explicit padding).

Example (proposed for Phase 2):

```cpp
struct Light {
    glm::vec4 directionOrPosition; // w = type or range
    glm::vec4 colorAndIntensity;
    // cone angles etc. for spots
    uint32_t type;
    uint32_t padding[3];
};
```

Uploaded via the same double-buffered `AllocatedBuffer` + `update_buffers` / `toggle_buffers` / `bind_descriptor` pattern used by all other managers.

### 5.3 IBL Resources (Phase 3)
- Diffuse: either 9 vec3 SH coeffs in the UBO above, or a small bindless texture.
- Specular: one cubemap (mip chain) + one 2D BRDF LUT texture.
- Can live in the existing bindless texture array (highest binding) or get dedicated sampler bindings if driver pressure appears on Quest.

## 6. Shader & Pipeline Integration

- All lighting math lives in `pbr.frag` (keep a `lighting.glsl` include later if it grows).
- No vertex shader changes required for lighting.
- Current single subpass is sufficient through Phase 2.
- When we adopt the multi-subpass target, a "lighting" subpass can read depth/normal/albedo via input attachments and accumulate lighting with fewer attribute fetches.

## 7. Risks & Mitigations

- **Performance on Adreno**: Start with 1–2 lights + very cheap IBL (SH + 1–2 specular taps). Profile early with Meta GPU Profiler.
- **Asset pipeline for IBL**: Document the exact offline process (resolution, compression, mipmap count) so desktop and Quest builds stay in sync.
- **Descriptor pressure**: Adding many IBL textures could hit limits; design for sharing the existing bindless array.
- **glTF light support**: The extension is optional in many exporters — treat lights as "nice to have" and always support engine-authored fallback lights.

## 8. Verification

- **Visual**: DamagedHelmet and Sponza (or other multi-material assets) should show plausible rough metal, dielectric response, and environment-independent but correctly shaped highlights.
- **Build**: `cmake --build` + `compile_shaders` target succeeds.
- **Regression**: Existing debug modes, emissive, AO, and normal mapping continue to work.
- **Doc quality**: This document + cross-references in `current_state.md` etc. are sufficient for a new engineer/agent to continue with Phase 2 without re-deriving the current state.

## 9. Known Rough Edges (as of end of Phase 2 work)

These were discovered during Phase 2 implementation and the subsequent shutdown crash investigation:

1. **VMA leak / assertion on shutdown** (critical, now fixed):
   - The `frame_globals_buffer` pair was not being destroyed. Fixed in `destroy_buffers()` + improved `cleanup_scene()`.

2. **Globals double-buffering is incomplete**:
   - We allocate two sides and have `globals_upload` / `globals_render` indices, but we do not perform a proper `toggle_buffers()` + descriptor rebind on scene load like the other managers.
   - We currently write the descriptor every single frame in `render()` (works but not ideal).

3. **No support for dynamic scene reload**:
   - Calling `load_scene()` multiple times without destroying the engine will accumulate or corrupt the globals buffers.

4. **Spot light implementation is incomplete**:
   - Direction for spots is not stored separately from position in the current packing (shader has a hack).

5. **Dead code**:
   - The `cameraPos` field we added to the push constant in Phase 1 is now superseded by the value in the `FrameGlobals` UBO. It can be cleaned up later.

6. **Lighting data is not yet part of the main SceneInstance / future GameObject model**:
   - Lights are currently a flat list on `Renderer`. When we move to the full `GameObject` + `TransformManager` architecture, lights should be integrated properly (with transforms, etc.).

7. **Spot light direction packing remains a known limitation** (documented in shader and Light.h):
   - Direction for spots re-uses the position field in the current UBO layout; real separate direction storage + proper cone math is still TODO for full spot support.

8. **Scene lights are static after load**:
   - No per-frame mutation of scene light positions/colors yet (globalLight fallback can still be tweaked live when no scene lights). Dynamic lights will need dirty tracking + proper double-buffer toggle for the globals.

9. **Globals double-buffering / reload hygiene** (pre-existing):
   - See earlier items in this list; still present.

10. **The entire FrameGlobals UBO + fixed light array design is temporary scaffolding**:
    - The recent std140 padding workaround (oversized `padding0[10]` / `padding1[10]` in C++ `gfx::FrameGlobals` to match GLSL `uniform` block array stride rules) and the need for per-frame `memset` + selective field writes are symptoms of the design.
    - Layout fragility, limited light count, awkward update path, and mixing tiny scalars with light arrays in one UBO under std140 rules make this unsuitable as a long-term solution.
    - The next step in the project is to implement a proper lighting solution (small per-frame constants UBO + dedicated lights SSBO with std430/clean layout, dynamic updates, larger/many lights, full IBL, and better scene integration). Do not extend the current UBO approach significantly without a design pass.

These (plus IBL, shadows, many-light clustering, and the proper lighting architecture) are the remaining lighting work items.

## Investigation in Progress (as of end of session)
The data path for scene lights is complete and the diagnostics confirm values are reaching the shader. However, the point light in `DamagedHelmetScene.gltf` does not illuminate the model. The user is investigating glTF import correctness (node hierarchy, world transform accumulation for lights, physical intensity handling, and whether range=0 point lights need an explicit inverse-square term in the shader).

Temporary diagnostic prints exist in `Engine.InitializeScene.cpp` for:
- `[LIGHT-TRANSFORM]` (after applying node world transform to each extracted light)
- Expanded `[LIGHTS]` block (exact values written into both sides of the FrameGlobals double buffer)

These (plus the existing full hierarchy dump and camera pointing diagnostics) should be removed or guarded once the import bug is resolved.

## Coordinate & Transform Notes for glTF Lights
- All node world transforms are accumulated exactly once on the CPU during `load_scene` in the `add_mesh_node` traversal (meshes, cameras, and lights use the identical final composed matrix). The engine is strictly faithful to the glTF data (no global post-correction matrices).
- **Directional lights** (per KHR_lights_punctual spec): the node orients a `(0,0,-1)` local emission direction (rays travel along local -Z). We store the opposite in `Light.positionOrDirection` (`= normalize(rot[2])`, the node's local +Z in world). This matches the `L` vector the shader expects for `NdotL = dot(N, L)`.
- Point/spot positions are taken directly from the translation column of the node's final world matrix. Rotation/scale on the light node are ignored for position (per spec).

**Historical note (2026)**: A global `BLENDER_CORRECTION` matrix (–90° X) was previously applied to every node's world transform to paper over common exporter artifacts from Blender. It was removed because it altered authored positions (especially lights) in standard +Y-up / +Z-forward glTF exports, producing incorrect lighting and camera framing relative to what the artist saw. The engine now trusts the glTF data exactly. If an asset needs correction, it should be fixed at export time in the DCC tool.

## References & Further Reading

- Real-Time Rendering 4th ed. (microfacet BRDF chapter)
- "Real Shading in Unreal Engine 4" (Karis) — the split-sum + prefilter technique
- glTF `KHR_lights_punctual` spec
- Qualcomm Adreno Best Practices for Vulkan on mobile
- Meta Horizon OS / Quest Vulkan performance guides (tile memory, GMEM)

---

**Current State (end of session, 2026)**: 
- Scene `KHR_lights_punctual` lights are extracted during `GltfLoader::extract_light_data`, their final world transforms (from the same `add_mesh_node` hierarchy traversal used for everything else) are captured, and the resulting values are written into the temporary `FrameGlobals` UBO (binding 0). The multi-light GGX loop in `pbr.frag` consumes them.
- The `FrameGlobals` + fixed light arrays + std140 padding approach (including the C++ padding array sizing workaround) is explicitly temporary scaffolding. The next step in the project is a proper lighting solution (small per-frame constants UBO + dedicated lights SSBO/buffer with clean `std430` or scalar layout, dynamic updates, better scalability, full IBL, etc.).
- A global `BLENDER_CORRECTION` matrix was introduced during camera/light debugging and has been completely removed. The engine now applies *only* the transforms exactly as present in the glTF file (policy: strict fidelity; export-time fixes only).
- The count + per-light diagnostic logging (added during investigation) shows that the DamagedHelmetScene point light reaches the shader, but it produces no visible contribution on the model. The user is actively debugging why the asset is not importing correctly (likely transform, position, or intensity/falloff issues).
- Engine `globalLight` fallback remains available for scenes without lights.

See the "Historical note" in the Coordinate & Transform section above, the "Investigation in Progress" section, and the status note at the top of this document for details.

**Maintained as of 2026**. Update this file when the active (temporary) lighting model changes, when the proper lighting architecture is designed, or when the import investigation concludes.