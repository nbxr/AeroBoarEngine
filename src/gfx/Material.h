#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include "core/Handle.h"

namespace gfx {

// Material structure for PBR rendering.
// Uploaded as a packed SSBO array; must match shaders/pbr.frag Material under std430.
// std430 base alignment is 16 (vec4) so array stride is 80 even though fields end at 76.
// alignas(16) forces C++ sizeof/stride to match that GPU layout (unaligned was 76).
struct alignas(16) Material {
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
    uint32_t emissive_texture_index {NO_TEXTURE};  // Index into bindless texture array
    uint32_t ao_texture_index {NO_TEXTURE};        // Ambient Occlusion (separate in this model)

    // Sampler indices (if needed for non-bindless approach)
    uint32_t sampler_index {NO_TEXTURE}; // Index into bindless sampler array

    // Flags/variant bits for uber-shader branching
    // Bit 0: HasAlbedoTexture
    // Bit 1: HasNormalMap
    // Bit 2: HasORMTexture
    // Bit 3: HasEmissiveTexture
    // Bit 4: IsEmissive
    // Bit 5: Transparent
    // Bit 6: NormalMapFlipY (invert green channel - useful for some authored normal maps)
    uint32_t flags; // Material flags for shader branching

    // Explicit pad to end of logical fields (offset 60..75); trailing alignas pad → 80.
    uint32_t padding[4];
};

// Must match shaders/pbr.frag Material + std430 array stride (base align 16 → 80).
static_assert(sizeof(Material) == 80, "Material must be 80 bytes for GPU SSBO layout");
static_assert(alignof(Material) == 16, "Material must be 16-byte aligned for SSBO array stride");

} // namespace gfx

// Material ID type for indexing into material arrays (in gfx namespace for consistency)
namespace gfx {
using MaterialID = core::Handle<Material>;
} // namespace gfx