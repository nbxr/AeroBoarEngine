#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace core {

// Material structure for PBR rendering
// This struct is designed to be compact and cache-friendly
// All data is uploaded to GPU in one go for maximum performance
struct Material {
    // Base color (albedo)
    glm::vec4 albedo;
    
    // PBR parameters
    float roughness;
    float metallic;
    float emissive;
    float normalStrength;
    
    // Texture indices for bindless descriptor indexing
    uint32_t albedoTextureIndex;    // Index into bindless texture array
    uint32_t normalTextureIndex;    // Index into bindless texture array
    uint32_t ormTextureIndex;       // Index into bindless texture array (Occlusion, Roughness, Metallic)
    uint32_t emissiveTextureIndex;  // Index into bindless texture array
    
    // Sampler indices (if needed for non-bindless approach)
    uint32_t samplerIndex;          // Index into bindless sampler array
    
    // Flags/variant bits for uber-shader branching
    uint32_t flags;                 // Material flags for shader branching
    
    // Padding to ensure proper alignment
    uint32_t padding[3];
};

// Material ID type for indexing into material arrays
using MaterialID = uint32_t;

} // namespace core