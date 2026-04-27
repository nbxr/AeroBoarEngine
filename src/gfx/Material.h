#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include "core/Handle.h"

namespace gfx {

// Material structure for PBR rendering
// This struct is designed to be compact and cache-friendly
// All data is uploaded to GPU in one go for maximum performance
struct Material {
    static constexpr uint32_t NO_TEXTURE = UINT32_MAX;

    // Base color (albedo)
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
    uint32_t emissive_texture_index {NO_TEXTURE} ; // Index into bindless texture array

    // Sampler indices (if needed for non-bindless approach)
    uint32_t sampler_index {NO_TEXTURE}; // Index into bindless sampler array

    // Flags/variant bits for uber-shader branching
    // Bit 0: HasAlbedoTexture
    // Bit 1: HasNormalMap
    // Bit 2: HasORMTexture
    // Bit 3: HasEmissiveTexture
    // Bit 4: IsEmissive
    // Bit 5: Transparent
    uint32_t flags; // Material flags for shader branching

    // Padding to ensure 16-byte alignment for SSBO (80 bytes total)
    uint32_t padding[4];
};

} // namespace gfx

// Material ID type for indexing into material arrays
using MaterialID = Handle<gfx::Material>;