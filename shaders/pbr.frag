#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Bindless resources
layout(set = 0, binding = 1) readonly buffer SceneInstances {
    uint material_index;
    uint mesh_index;
    uint flags;
    mat4 transform;
} scene_instances[];

layout(set = 0, binding = 2) readonly buffer Materials {
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
} materials[];

layout(set = 0, binding = 6) uniform sampler2D bindlessTextures[];  // Variable count

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;

// Push constants (must match pbr.vert)
layout(push_constant) uniform PushConstants {
    mat4   viewProj;
    mat4   model;
    uvec4  extra;   // x = materialIndex
} pc;

layout(location = 0) out vec4 outColor;

// Simple global overhead lighting for Phase 1 (per user request)
// TODO (later phase): Replace with proper IBL or more sophisticated lighting
const vec3  LIGHT_DIR   = normalize(vec3(0.0, -1.0, 0.0)); // Directly overhead
const vec3  LIGHT_COLOR = vec3(1.0, 0.98, 0.95);
const float LIGHT_INTENSITY = 1.0;
const vec3  AMBIENT       = vec3(0.03);

const uint NO_TEXTURE = 0xFFFFFFFFu;

vec3 getNormalFromMap(vec3 N, vec3 T, vec3 B, vec2 uv, uint normalTexIdx, float strength) {
    if (normalTexIdx == NO_TEXTURE) {
        return normalize(N);
    }
    vec3 normalMap = texture(bindlessTextures[nonuniformEXT(normalTexIdx)], uv).xyz * 2.0 - 1.0;
    normalMap.xy *= strength;
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * normalMap);
}

void main() {
    // Use material index pushed per draw (Phase 1)
    uint matIdx = pc.extra.x;
    // Note: Runtime .length() on unsized SSBO array not supported in this GLSL version without extra setup.
    // Relying on CPU-side validation for now.

    vec4  baseColor   = materials[matIdx].albedo;
    float roughness   = materials[matIdx].roughness;
    float metallic    = materials[matIdx].metallic;
    uint  albedoIdx   = materials[matIdx].albedo_texture_index;
    uint  normalIdx   = materials[matIdx].normal_texture_index;
    uint  ormIdx      = materials[matIdx].roughness_texture_index;
    uint  emissiveIdx = materials[matIdx].emissive_texture_index;
    uint  aoIdx       = materials[matIdx].ao_texture_index;
    float normalStr   = materials[matIdx].normalStrength;

    // Sample albedo
    vec4 albedo = baseColor;
    if (albedoIdx != NO_TEXTURE) {
        albedo = texture(bindlessTextures[nonuniformEXT(albedoIdx)], inUV);
    }

    // Sample metallic-roughness (typical glTF layout: R=unused, G=roughness, B=metallic)
    float sampledRough = roughness;
    float sampledMetal = metallic;
    if (ormIdx != NO_TEXTURE) {
        vec3 orm = texture(bindlessTextures[nonuniformEXT(ormIdx)], inUV).rgb;
        sampledRough = orm.g;
        sampledMetal = orm.b;
    }

    // Normal mapping
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent.xyz);
    vec3 B = normalize(cross(N, T) * inTangent.w);
    N = getNormalFromMap(N, T, B, inUV, normalIdx, normalStr);

    // Simple lighting - overhead directional
    vec3 L = LIGHT_DIR;
    vec3 V = normalize(-inWorldPos); // Viewer at origin approximation for now
    vec3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Very simplified PBR (Lambert diffuse + basic specular)
    vec3 diffuse  = albedo.rgb * (1.0 - sampledMetal) * NdotL;
    vec3 specular = vec3(pow(NdotH, mix(2.0, 64.0, 1.0 - sampledRough))) * sampledMetal * NdotL;

    vec3 color = (diffuse + specular) * LIGHT_COLOR * LIGHT_INTENSITY;
    color += albedo.rgb * AMBIENT;

    // Emissive
    if (emissiveIdx != NO_TEXTURE) {
        vec3 emissiveColor = texture(bindlessTextures[nonuniformEXT(emissiveIdx)], inUV).rgb;
        color += emissiveColor * materials[matIdx].emissive;
    }

    // Ambient Occlusion
    float ao = 1.0;
    if (aoIdx != NO_TEXTURE) {
        ao = texture(bindlessTextures[nonuniformEXT(aoIdx)], inUV).r;
    }
    color *= ao;

    outColor = vec4(color, albedo.a);

    // === TEMP DIAGNOSTIC ===
    // Color the surface based on material_index to verify per-part materials are different.
    // If all parts are the same color, then material_index is not varying per draw.
    int mid = int(matIdx) % 6;
    vec3 debugCol;
    if (mid == 0) debugCol = vec3(1, 0, 0);
    else if (mid == 1) debugCol = vec3(0, 1, 0);
    else if (mid == 2) debugCol = vec3(0, 0, 1);
    else if (mid == 3) debugCol = vec3(1, 1, 0);
    else if (mid == 4) debugCol = vec3(1, 0, 1);
    else debugCol = vec3(0, 1, 1);

    // DIAGNOSTIC (commented out for normal rendering)
    // outColor = vec4(debugCol, 1.0);
}