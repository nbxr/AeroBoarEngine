#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace gfx {

constexpr uint32_t NO_TEXTURE = 0xFFFFFFFFu;

// Light types matching KHR_lights_punctual + engine needs
enum class LightType : uint32_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
};

// CPU-side light description (used during loading and management).
// Compact and matches what we upload to the GPU for Phase 2.
struct Light {
    LightType type = LightType::Directional;

    // For directional: direction (normalized)
    // For point/spot: world position
    glm::vec3 positionOrDirection {0.0f, -1.0f, 0.0f};

    glm::vec3 color {1.0f, 1.0f, 1.0f};
    float     intensity = 1.0f;

    // Point / Spot
    float range = 0.0f;   // 0 = infinite (for directional this is ignored)

    // Spot only (in radians)
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.7853981634f; // ~45 degrees

    uint32_t padding = 0; // keep 16-byte alignment friendly when in arrays
};

// GPU layout for the per-frame globals UBO (binding 0).
// This is what the shader will read. Keep it std140 friendly.
// Fixed maximum for Phase 2 (easy, cache friendly, no variable array headaches yet).
constexpr uint32_t MAX_LIGHTS = 8;

struct FrameGlobals {
    glm::vec4 cameraPosition;   // xyz = camera world pos, w = unused (or exposure later)
    float     exposure = 1.0f;
    uint32_t  lightCount = 0;
    // NOTE: padding arrays are sized larger than the logical [2] in the GLSL
    // uniform block because std140 requires arrays of scalars to use 16-byte
    // stride and base alignment (rounded up to vec4). This makes the GLSL
    // compiler place subsequent vec4 arrays (and the "padding0" array itself)
    // at higher offsets (lights start at 64, not 32). We replicate that layout
    // here so that field writes via this struct land at the byte offsets the
    // shader's uniform block expects. See pbr.frag FrameGlobals and SPIR-V
    // OpMemberDecorate offsets.
    uint32_t  padding0[10] = {};   // sized to place following arrays at offset 64

    // Packed light data (std140 friendly) -- offsets now match shader decl
    glm::vec4 lightDirectionsOrPositions[MAX_LIGHTS];
    glm::vec4 lightColors[MAX_LIGHTS];
    glm::vec4 lightParams[MAX_LIGHTS];

    // === Phase 3 IBL data ===
    // Diffuse irradiance as 3-band spherical harmonics (9 coefficients)
    glm::vec4 shCoefficients[9];   // .rgb = coeff, .a unused

    // Indices into the bindless texture array (NO_TEXTURE = 0xFFFFFFFFu means "not available")
    uint32_t specularEnvMapIndex = 0xFFFFFFFFu;  // prefiltered GGX cubemap (mip chain)
    uint32_t brdfLutIndex        = 0xFFFFFFFFu;  // 2D BRDF integration LUT

    uint32_t padding1[10] = {};    // sized to cover GLSL padding1 array placement at 608+
};

static_assert(sizeof(FrameGlobals) <= 1024, "FrameGlobals should stay reasonably small for UBO");

} // namespace gfx
