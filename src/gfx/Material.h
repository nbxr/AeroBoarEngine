#pragma once

#include <cstdint>
#include <cstring>
#include <glm/glm.hpp>
#include "core/Handle.h"

namespace gfx {

// Material structure for PBR rendering.
// Uploaded as a packed SSBO array; must match shaders/pbr.frag Material under std430.
// std430 base alignment is 16 (vec4) so array stride is 80 even though fields end at 76.
// alignas(16) forces C++ sizeof/stride to match that GPU layout (unaligned was 76).
struct alignas(16) Material {
    static constexpr uint32_t NO_TEXTURE = UINT32_MAX;

    // flags bitfield (must match shaders/pbr.frag)
    static constexpr uint32_t kFlagHasAlbedoTex   = 1u << 0;
    static constexpr uint32_t kFlagHasNormalMap   = 1u << 1;
    static constexpr uint32_t kFlagHasOrmTex      = 1u << 2;
    static constexpr uint32_t kFlagHasEmissiveTex = 1u << 3;
    static constexpr uint32_t kFlagIsEmissive     = 1u << 4;
    static constexpr uint32_t kFlagAlphaBlend     = 1u << 5; // glTF alphaMode = BLEND
    static constexpr uint32_t kFlagNormalFlipY    = 1u << 6;
    static constexpr uint32_t kFlagAlphaMask      = 1u << 7; // glTF alphaMode = MASK
    static constexpr uint32_t kFlagDoubleSided    = 1u << 8;

    // Base color (albedo) — rgb * texture, a = opacity (glTF baseColorFactor/texture)
    glm::vec4 albedo;

    // PBR parameters
    float roughness;
    float metallic;
    float emissive;
    float normalStrength;

    // Texture indices for bindless descriptor indexing
    uint32_t albedo_texture_index {NO_TEXTURE}; // Index into bindless texture array
    uint32_t normal_texture_index {NO_TEXTURE}; // Index into bindless texture array
    uint32_t roughness_texture_index {NO_TEXTURE}; // Index into bindless texture array (Occlusion,
                                 // Roughness, Metallic)
    uint32_t emissive_texture_index {NO_TEXTURE};  // Index into bindless texture array
    uint32_t ao_texture_index {NO_TEXTURE};        // Ambient Occlusion (separate in this model)

    // Sampler indices (if needed for non-bindless approach)
    uint32_t sampler_index {NO_TEXTURE}; // Index into bindless sampler array

    uint32_t flags = 0;

    // padding[0] = floatBitsToUint(alphaCutoff) for MASK (default 0.5). [1..3] reserved.
    uint32_t padding[4]{};

    void set_alpha_cutoff(float cutoff) {
        std::memcpy(&padding[0], &cutoff, sizeof(float));
    }
    [[nodiscard]] float alpha_cutoff() const {
        float c = 0.5f;
        std::memcpy(&c, &padding[0], sizeof(float));
        return c;
    }
};

// Must match shaders/pbr.frag Material + std430 array stride (base align 16 → 80).
static_assert(sizeof(Material) == 80, "Material must be 80 bytes for GPU SSBO layout");
static_assert(alignof(Material) == 16, "Material must be 16-byte aligned for SSBO array stride");

} // namespace gfx

// Material ID type for indexing into material arrays (in gfx namespace for consistency)
namespace gfx {
using MaterialID = core::Handle<Material>;
} // namespace gfx