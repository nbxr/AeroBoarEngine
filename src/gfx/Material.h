#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include "core/Handle.h"

namespace gfx {

// PBR material SSBO element — must match shaders/pbr.frag under std430.
// 256-byte stride for maps, multi-UV + KHR_texture_transform, and material extensions.
struct alignas(16) Material {
    static constexpr uint32_t NO_TEXTURE = UINT32_MAX;

    static constexpr uint32_t kFlagHasAlbedoTex   = 1u << 0;
    static constexpr uint32_t kFlagHasNormalMap   = 1u << 1;
    static constexpr uint32_t kFlagHasOrmTex      = 1u << 2;
    static constexpr uint32_t kFlagHasEmissiveTex = 1u << 3;
    static constexpr uint32_t kFlagIsEmissive     = 1u << 4;
    static constexpr uint32_t kFlagAlphaBlend     = 1u << 5;
    static constexpr uint32_t kFlagNormalFlipY    = 1u << 6;
    static constexpr uint32_t kFlagAlphaMask      = 1u << 7;
    static constexpr uint32_t kFlagDoubleSided    = 1u << 8;
    static constexpr uint32_t kFlagClearcoat      = 1u << 9;
    static constexpr uint32_t kFlagTransmission   = 1u << 10;
    static constexpr uint32_t kFlagIridescence    = 1u << 11;

    static constexpr uint32_t kUvAlbedo   = 0;
    static constexpr uint32_t kUvNormal   = 1;
    static constexpr uint32_t kUvOrm      = 2;
    static constexpr uint32_t kUvEmissive = 3;
    static constexpr uint32_t kUvAo       = 4;

    // 0
    glm::vec4 albedo{1.f};

    // 16
    float roughness = 1.f;
    float metallic = 1.f;
    float normalStrength = 1.f;
    float clearcoat = 0.f; // clearcoatFactor

    // 32 — emissive RGB + KHR_materials_emissive_strength in .w (default 1)
    glm::vec4 emissive_factor{0.f, 0.f, 0.f, 1.f};

    // 48
    uint32_t albedo_texture_index{NO_TEXTURE};
    uint32_t normal_texture_index{NO_TEXTURE};
    uint32_t roughness_texture_index{NO_TEXTURE};
    uint32_t emissive_texture_index{NO_TEXTURE};

    // 64
    uint32_t ao_texture_index{NO_TEXTURE};
    uint32_t sampler_index{NO_TEXTURE};
    uint32_t flags = 0;
    uint32_t texcoord_packed = 0; // 4 bits per slot 0..4

    // 80
    float alpha_cutoff = 0.5f;
    float clearcoat_roughness = 0.f;
    float transmission = 0.f;
    float iridescence = 0.f;

    // 96
    float iridescence_ior = 1.3f;
    float iridescence_thickness = 400.f; // nm (simple single thickness)
    float _pad0 = 0.f;
    float _pad1 = 0.f;

    // 112 — KHR_texture_transform: xy=scale, zw=offset per slot
    glm::vec4 uv_scale_offset[5]{
        {1.f, 1.f, 0.f, 0.f}, {1.f, 1.f, 0.f, 0.f}, {1.f, 1.f, 0.f, 0.f},
        {1.f, 1.f, 0.f, 0.f}, {1.f, 1.f, 0.f, 0.f},
    };

    // 192
    float uv_rotation[5]{0.f, 0.f, 0.f, 0.f, 0.f};
    float _rot_pad[3]{0.f, 0.f, 0.f};

    // 224 → 256
    uint32_t _tail[8]{};

    void set_texcoord(uint32_t slot, uint32_t set) {
        if (slot > 4)
            return;
        const uint32_t shift = slot * 4u;
        texcoord_packed =
            (texcoord_packed & ~(0xFu << shift)) | ((set & 0xFu) << shift);
    }
    [[nodiscard]] uint32_t texcoord(uint32_t slot) const {
        if (slot > 4)
            return 0;
        return (texcoord_packed >> (slot * 4u)) & 0xFu;
    }

    void set_uv_transform(uint32_t slot, float scale_u, float scale_v,
                          float offset_u, float offset_v, float rotation) {
        if (slot > 4)
            return;
        uv_scale_offset[slot] = glm::vec4(scale_u, scale_v, offset_u, offset_v);
        uv_rotation[slot] = rotation;
    }
};

static_assert(sizeof(Material) == 256, "Material must be 256 bytes for GPU SSBO");
static_assert(alignof(Material) == 16, "Material must be 16-byte aligned");

} // namespace gfx

namespace gfx {
using MaterialID = core::Handle<Material>;
} // namespace gfx
