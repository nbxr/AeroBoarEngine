#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace gfx {

constexpr uint32_t NO_TEXTURE = 0xFFFFFFFFu;
constexpr uint32_t kInvalidLightTransform = 0xFFFFFFFFu;

// Light types matching KHR_lights_punctual + engine needs
enum class LightType : uint32_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
};

// CPU-side light (load-time, engine fallback, and runtime mutation).
// Written to GPU every frame via write_frame_lighting().
struct Light {
    LightType type = LightType::Directional;

    // Point / spot: world-space position.
    glm::vec3 position{0.0f};

    // Directional: *to-light* vector L used for NdotL (KHR: emission is −Z, so
    // L = world +Z of the node). Spot: emission direction (world −Z of node).
    glm::vec3 direction{0.35f, 1.0f, 0.25f};

    glm::vec3 color{1.0f, 0.98f, 0.95f};
    float     intensity = 3.0f;

    // Point / Spot: 0 = infinite range (KHR inverse-square only)
    float range = 0.0f;

    // Spot only (radians, half-angles from center — KHR_lights_punctual)
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.7853981634f; // ~45 degrees

    // Optional TransformManager index for refresh_lights_from_transforms().
    uint32_t transform_index = kInvalidLightTransform;

    // When false, skipped by write_frame_lighting (still occupies CPU list).
    bool enabled = true;
};

// Fixed maximum analytic lights in the per-frame lights SSBO.
constexpr uint32_t MAX_LIGHTS = 8;

// GPU light record (std430 SSBO element). 64 bytes.
struct GpuLight {
    glm::vec4 position;       // xyz = world pos (point/spot); unused for directional
    glm::vec4 direction;      // xyz = L (dir) or emission dir (spot)
    glm::vec4 colorIntensity; // rgb + intensity (candela / lux per KHR)
    glm::vec4 params;         // x=type, y=range, z=cos(inner), w=cos(outer)
};
static_assert(sizeof(GpuLight) == 64, "GpuLight must be 64 bytes (std430)");

// Small per-frame constants UBO (binding 0). Only vec4/uvec4 — no scalar arrays,
// so std140 matches C++ with no padding hacks.
struct FrameConstants {
    glm::vec4 cameraPosition; // xyz = world camera, w = exposure
    glm::uvec4 lightMeta;     // x = lightCount (0..MAX_LIGHTS)
    glm::vec4 shCoefficients[9];
    glm::uvec4 iblIndices; // x = specularEnvMapIndex, y = brdfLutIndex
    // Directional shadow (one map, shared by both eyes later). mat4 = 4×vec4.
    glm::mat4 shadowViewProj{1.0f};
    glm::vec4 shadowParams{0.0f}; // x=texel UV, y=enabled, z=light index, w=bias
};
static_assert(sizeof(FrameConstants) == 272, "FrameConstants size mismatch");
static_assert(offsetof(FrameConstants, lightMeta) == 16, "FrameConstants layout");
static_assert(offsetof(FrameConstants, shCoefficients) == 32, "FrameConstants layout");
static_assert(offsetof(FrameConstants, iblIndices) == 176, "FrameConstants layout");
static_assert(offsetof(FrameConstants, shadowViewProj) == 192, "FrameConstants layout");
static_assert(offsetof(FrameConstants, shadowParams) == 256, "FrameConstants layout");

// Estimate unshaded peak contribution used for auto-exposure (before NdotL / PI).
inline float estimate_light_contribution(const Light& L, const glm::vec3& scene_center) {
    if (!L.enabled)
        return 0.0f;
    const float I = std::max(L.intensity, 0.0f);
    if (L.type == LightType::Directional) {
        return I;
    }
    // Point / spot: KHR candela with inverse-square falloff at scene center
    const glm::vec3 d = L.position - scene_center;
    const float d2 = std::max(glm::dot(d, d), 0.01f);
    return I / d2;
}

// Choose exposure so the strongest light is about `target` at the scene center
// before BRDF / NdotL. Photometric KHR point lights (e.g. ~54k cd at ~7 m)
// need strong scale-down; engine directional lights with intensity ~1–10 stay
// near exposure 1 so they are not over-boosted into washout.
inline float compute_auto_exposure(const Light* lights, uint32_t count,
                                   const glm::vec3& scene_center,
                                   float target = 2.0f) {
    float max_c = 0.0f;
    bool any_punctual = false;
    for (uint32_t i = 0; i < count; ++i) {
        const Light& L = lights[i];
        if (!L.enabled)
            continue;
        max_c = std::max(max_c, estimate_light_contribution(L, scene_center));
        if (L.type != LightType::Directional)
            any_punctual = true;
    }
    if (max_c < 1e-4f)
        return 1.0f;
    // Engine-scale directional only: keep authored intensity as-is.
    if (!any_punctual && max_c <= 20.0f)
        return 1.0f;
    return target / max_c;
}

inline float compute_auto_exposure(const std::vector<Light>& lights,
                                   const glm::vec3& scene_center,
                                   float target = 2.0f) {
    return compute_auto_exposure(lights.data(), static_cast<uint32_t>(lights.size()),
                                 scene_center, target);
}

inline GpuLight to_gpu_light(const Light& L) {
    GpuLight g{};
    g.position = glm::vec4(L.position, 0.0f);
    g.direction = glm::vec4(glm::length(L.direction) > 1e-6f ? glm::normalize(L.direction)
                                                             : glm::vec3(0.0f, 1.0f, 0.0f),
                            0.0f);
    g.colorIntensity = glm::vec4(L.color, L.intensity);

    // Pack cosines of half-angles for stable spot falloff in the shader.
    float cos_inner = 1.0f;
    float cos_outer = 0.0f;
    if (L.type == LightType::Spot) {
        const float inner = std::clamp(L.innerConeAngle, 0.0f, L.outerConeAngle);
        const float outer = std::max(L.outerConeAngle, inner + 1e-4f);
        cos_inner = std::cos(inner);
        cos_outer = std::cos(outer);
        // Ensure cos_inner >= cos_outer (inner cone is narrower).
        if (cos_inner < cos_outer)
            std::swap(cos_inner, cos_outer);
    }

    g.params = glm::vec4(static_cast<float>(L.type), L.range, cos_inner, cos_outer);
    return g;
}

// Legacy alias while call sites migrate (prefer FrameConstants).
using FrameGlobals = FrameConstants;

} // namespace gfx
