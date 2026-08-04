#version 460
#extension GL_EXT_nonuniform_qualifier : require

// Depth-only prepass fragment stage: honor glTF MASK / BLEND so Hi-Z and
// depth do not treat cutouts or fully transparent fragments as solid.

struct Material {
    vec4  albedo;
    float roughness;
    float metallic;
    float emissive;
    float normalStrength;
    uint  albedo_texture_index;
    uint  normal_texture_index;
    uint  roughness_texture_index;
    uint  emissive_texture_index;
    uint  ao_texture_index;
    uint  sampler_index;
    uint  flags;
    uint  padding[4];
};

layout(set = 0, binding = 2) readonly buffer Materials {
    Material materials[];
};

layout(set = 0, binding = 10) uniform sampler2D bindlessTextures[];

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
layout(location = 4) flat in uint inMaterialIndex;

const uint NO_TEXTURE = 0xFFFFFFFFu;

void main() {
    uint matIdx = inMaterialIndex;
    uint flags = materials[matIdx].flags;

    // BLEND surfaces: do not write depth in prepass (main pass blends over).
    if ((flags & 0x20u) != 0u)
        discard;

    // OPAQUE: always write depth (no alpha test).
    if ((flags & 0x80u) == 0u)
        return;

    // MASK: sample base color alpha and discard below cutoff.
    vec4 albedo = materials[matIdx].albedo;
    uint albedoIdx = materials[matIdx].albedo_texture_index;
    if (albedoIdx != NO_TEXTURE) {
        albedo *= texture(bindlessTextures[nonuniformEXT(albedoIdx)], inUV);
    }
    float cutoff = uintBitsToFloat(materials[matIdx].padding[0]);
    if (albedo.a < cutoff)
        discard;
}
