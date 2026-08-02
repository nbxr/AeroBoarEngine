#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace gfx {

constexpr uint32_t NO_TEXTURE = 0xFFFFFFFFu;

// Light types matching KHR_lights_punctual + engine needs
enum class LightType : uint32_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
};

// CPU-side light description (load-time + engine fallback).
struct Light {
    LightType type = LightType::Directional;

    // Directional: to-light direction (normalized), used as L in the shader.
    // Point/spot: world position.
    // Default = slightly angled overhead so +Y-facing surfaces receive light.
    glm::vec3 positionOrDirection{0.35f, 1.0f, 0.25f};

    glm::vec3 color{1.0f, 0.98f, 0.95f};
    float     intensity = 3.0f;

    // Point / Spot: 0 = infinite range (KHR inverse-square only)
    float range = 0.0f;

    // Spot only (radians)
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.7853981634f; // ~45 degrees

    uint32_t padding = 0;
};

// Fixed maximum analytic lights in the per-frame lights SSBO (Phase 2).
constexpr uint32_t MAX_LIGHTS = 8;

// GPU light record (std430 SSBO element). 48 bytes, naturally aligned.
struct GpuLight {
    glm::vec4 positionOrDirection; // xyz = dir or pos; w unused
    glm::vec4 colorIntensity;      // rgb + intensity (candela / lux per KHR)
    glm::vec4 params;              // x=type, y=range, z=innerCone, w=outerCone
};
static_assert(sizeof(GpuLight) == 48, "GpuLight must be 48 bytes (std430)");

// Small per-frame constants UBO (binding 0). Only vec4/uvec4 — no scalar arrays,
// so std140 matches C++ with no padding hacks.
struct FrameConstants {
    glm::vec4 cameraPosition; // xyz = world camera, w = exposure
    glm::uvec4 lightMeta;     // x = lightCount (0..MAX_LIGHTS)
    glm::vec4 shCoefficients[9];
    glm::uvec4 iblIndices; // x = specularEnvMapIndex, y = brdfLutIndex
};
static_assert(sizeof(FrameConstants) == 192, "FrameConstants size mismatch");
static_assert(offsetof(FrameConstants, lightMeta) == 16, "FrameConstants layout");
static_assert(offsetof(FrameConstants, shCoefficients) == 32, "FrameConstants layout");
static_assert(offsetof(FrameConstants, iblIndices) == 176, "FrameConstants layout");

// Estimate unshaded peak contribution used for auto-exposure (before NdotL / PI).
inline float estimate_light_contribution(const Light& L, const glm::vec3& scene_center) {
    const float I = std::max(L.intensity, 0.0f);
    if (L.type == LightType::Directional) {
        return I;
    }
    // Point / spot: KHR candela with inverse-square falloff
    const float d2 = std::max(glm::dot(L.positionOrDirection - scene_center,
                                       L.positionOrDirection - scene_center),
                              0.01f);
    return I / d2;
}

// Choose exposure so the strongest light is about `target` at the scene center
// before BRDF / NdotL. Photometric KHR point lights (e.g. ~54k cd at ~7 m)
// need strong scale-down; engine directional lights with intensity ~1–10 stay
// near exposure 1 so they are not over-boosted into washout.
inline float compute_auto_exposure(const std::vector<Light>& lights,
                                   const glm::vec3& scene_center,
                                   float target = 2.0f) {
    float max_c = 0.0f;
    bool any_punctual = false;
    for (const auto& L : lights) {
        max_c = std::max(max_c, estimate_light_contribution(L, scene_center));
        if (L.type != LightType::Directional) {
            any_punctual = true;
        }
    }
    if (max_c < 1e-4f) {
        return 1.0f;
    }
    // Engine-scale directional only: keep authored intensity as-is.
    if (!any_punctual && max_c <= 20.0f) {
        return 1.0f;
    }
    return target / max_c;
}

inline GpuLight to_gpu_light(const Light& L) {
    GpuLight g{};
    g.positionOrDirection = glm::vec4(L.positionOrDirection, 0.0f);
    g.colorIntensity = glm::vec4(L.color, L.intensity);
    g.params = glm::vec4(static_cast<float>(L.type), L.range, L.innerConeAngle,
                         L.outerConeAngle);
    return g;
}

// Legacy alias while call sites migrate (prefer FrameConstants).
using FrameGlobals = FrameConstants;

} // namespace gfx
