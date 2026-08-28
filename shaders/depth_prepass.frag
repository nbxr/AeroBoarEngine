#version 460
#extension GL_EXT_nonuniform_qualifier : require

// Depth-only prepass: MASK alpha test + skip BLEND. Material layout matches pbr.frag.

struct Material {
    vec4  albedo;
    float roughness;
    float metallic;
    float normalStrength;
    float clearcoat;
    vec4  emissive_factor;
    uint  albedo_texture_index;
    uint  normal_texture_index;
    uint  roughness_texture_index;
    uint  emissive_texture_index;
    uint  ao_texture_index;
    uint  sampler_index;
    uint  flags;
    uint  texcoord_packed;
    float alpha_cutoff;
    float clearcoat_roughness;
    float transmission;
    float iridescence;
    float iridescence_ior;
    float iridescence_thickness;
    float _pad0;
    float _pad1;
    vec4  uv_scale_offset[5];
    float uv_rotation[5];
    float _rot_pad[3];
    uint  _tail[8];
};

layout(set = 0, binding = 2) readonly buffer Materials {
    Material materials[];
};

layout(set = 0, binding = 11) uniform sampler2D bindlessTextures[];

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV0;
layout(location = 4) in vec2 inUV1;
layout(location = 5) flat in uint inMaterialIndex;
layout(location = 6) in vec4 inColor;

const uint NO_TEXTURE = 0xFFFFFFFFu;

vec2 apply_uv_transform(vec2 uv, vec4 scale_offset, float rotation) {
    float c = cos(rotation);
    float s = sin(rotation);
    vec2 scaled = uv * scale_offset.xy;
    vec2 rotated = vec2(c * scaled.x - s * scaled.y, s * scaled.x + c * scaled.y);
    return rotated + scale_offset.zw;
}

void main() {
    uint matIdx = inMaterialIndex;
    uint flags = materials[matIdx].flags;

    // Hi-Z / prepass: only true solid occluders.
    // BLEND and TRANSMISSION (glass) must not write depth — they seal window
    // openings and false-cull cabin geometry in the occlusion pass.
    if ((flags & 0x20u) != 0u) // ALPHA_BLEND
        discard;
    if ((flags & 0x400u) != 0u) // TRANSMISSION
        discard;

    if ((flags & 0x80u) == 0u) // not MASK → fully opaque, write depth
        return;

    vec4 albedo = materials[matIdx].albedo;
    uint albedoIdx = materials[matIdx].albedo_texture_index;
    if (albedoIdx != NO_TEXTURE) {
        uint set = (materials[matIdx].texcoord_packed >> 0u) & 0xFu;
        vec2 uv = (set == 0u) ? inUV0 : inUV1;
        uv = apply_uv_transform(uv, materials[matIdx].uv_scale_offset[0],
                                materials[matIdx].uv_rotation[0]);
        albedo *= texture(bindlessTextures[nonuniformEXT(albedoIdx)], uv);
    }
    albedo *= inColor;
    if (albedo.a < materials[matIdx].alpha_cutoff)
        discard;
}
