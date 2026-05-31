# Lighting Implementation Guide

**Status**: Phase 1 (improved analytic BRDF + better ambient) implemented.  
**Target**: High-quality, Quest 3 friendly PBR lighting with IBL and efficient multi-light support.  
**See also**:
- `docs/agents/current_state.md` (current gaps & priorities)
- `docs/architecture/pipeline-implementation.md` (subpass & overall pipeline vision)
- `docs/agents/tech_context.md` (descriptor bindings, per-frame globals)
- `docs/agents/architecture_principles.md` (Quest 3 TBDR rules)

## 1. Current State (as of Phase 1 work)

The renderer uses a single forward pass with `pbr.vert` + `pbr.frag`.

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

- **Single light** (hardcoded direction in shader for Phase 1).
- No dynamic or multiple lights from the scene.
- No image-based lighting (reflections, diffuse environment).
- Viewer position hack limits correctness on large scenes or when camera is not near origin.
- glTF `KHR_lights_punctual` lights are ignored by `GltfLoader` (nodes with lights are skipped in the traversal comments).
- No shadows (see `PassType::Shadow` scaffolding).

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

### Phase 1 — Analytic PBR + Multi-Light Foundation (Current Increment)
**Goals**:
- Proper GGX BRDF (done in shader).
- Support for a small number of lights (2–4 directional or simple points) from C++.
- Improved ambient (tinted constant or simple SH).
- Real camera position for view vector.

**Implementation notes** (this increment):
- Shader functions added (`D_GGX`, `F_Schlick`, `G_SmithGGX`).
- Still uses the original single `LIGHT_DIR` constant + push-constant-driven material data.
- Next engineering steps (see todos): activate binding 0 as a small `FrameGlobals` UBO containing light array + camera position, populate it from the engine, loop in the shader.

**Status**: BRDF math complete and verified to compile. Multi-light wiring is the immediate follow-up.

### Phase 2 — Full Analytic + glTF Lights
- Multiple light types (directional + point + spot) with proper attenuation and cone for spots.
- Load `KHR_lights_punctual` in `GltfLoader` (similar to materials).
- Small `gfx::Light` struct + upload path (reuse or lightly extend the MaterialManager double-buffer idiom).
- Real camera position passed every frame.
- Basic exposure / tone mapping hook.

### Phase 3 — IBL (Production Look)
- Diffuse irradiance (SH coefficients in the per-frame UBO or a tiny buffer).
- Specular prefiltered cubemap + BRDF LUT uploaded via TextureManager (bindless or dedicated bindings).
- Split sum approximation in the shader.
- Asset workflow documented for converting HDRis → IBL set (offline, using cmgen or similar).

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
    uint  lightCount;
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

## References & Further Reading

- Real-Time Rendering 4th ed. (microfacet BRDF chapter)
- "Real Shading in Unreal Engine 4" (Karis) — the split-sum + prefilter technique
- glTF `KHR_lights_punctual` spec
- Qualcomm Adreno Best Practices for Vulkan on mobile
- Meta Horizon OS / Quest Vulkan performance guides (tile memory, GMEM)

---

**Maintained as of 2026**. Update this file when a new lighting phase is completed or when major Quest 3 lighting extensions become relevant.