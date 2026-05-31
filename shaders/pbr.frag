#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Bindless resources
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
    uvec4  extra;      // x = materialIndex
    vec4   cameraPos;  // xyz = world-space camera position (w unused) — Phase 1 lighting
} pc;

layout(location = 0) out vec4 outColor;

// -----------------------------------------------------------------------------
// Lighting (Phase 1 improvements — two analytic lights + proper GGX BRDF)
// See docs/architecture/lighting-implementation.md for the full roadmap,
// current vs. target math, Quest 3 constraints, and data design.
// Phase 1: real Cook-Torrance + camera position + second fill light.
// Future: binding 0 per-frame lights UBO, IBL (SH + prefilter), glTF lights.
// -----------------------------------------------------------------------------

const vec3  LIGHT_DIR   = normalize(vec3(0.0, -1.0, 0.0)); // Directly overhead (Phase 1)
const vec3  LIGHT_COLOR = vec3(1.0, 0.98, 0.95);
const float LIGHT_INTENSITY = 1.0;

// Improved ambient for Phase 1 (still constant; will become SH/IBL later)
const vec3  AMBIENT       = vec3(0.02);   // slightly darker base
const vec3  AMBIENT_TINT  = vec3(0.95, 0.98, 1.05); // very subtle cool tint

const float PI = 3.14159265359;

// GGX / Trowbridge-Reitz normal distribution
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Schlick Fresnel approximation
vec3 F_Schlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Smith GGX geometry term (height-correlated)
float G_SmithGGX(float NdotV, float NdotL, float roughness) {
    float a = roughness * roughness;
    float k = (a + 1.0) * (a + 1.0) / 8.0;   // Schlick-GGX k for direct lighting
    float G1V = NdotV / (NdotV * (1.0 - k) + k);
    float G1L = NdotL / (NdotL * (1.0 - k) + k);
    return G1V * G1L;
}

const uint NO_TEXTURE = 0xFFFFFFFFu;

vec3 getNormalFromMap(vec3 N, vec3 T, vec3 B, vec2 uv, uint normalTexIdx, float strength, uint matFlags) {
    if (normalTexIdx == NO_TEXTURE) {
        return normalize(N);
    }
    vec3 normalMap = texture(bindlessTextures[nonuniformEXT(normalTexIdx)], uv).xyz * 2.0 - 1.0;

    // Optional green channel flip (bit 6 of material flags).
    // Many tools export DirectX-style normal maps (inverted green).
    // glTF recommends OpenGL convention (no flip), but this allows easy correction.
    if ((matFlags & 0x40u) != 0u) {  // Bit 6 = NormalMapFlipY
        normalMap.y = -normalMap.y;
    }

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
    N = getNormalFromMap(N, T, B, inUV, normalIdx, normalStr, materials[matIdx].flags);

    // -----------------------------------------------------------------
    // Phase 1 improved lighting — proper Cook-Torrance GGX BRDF + 2 lights
    // (see docs/architecture/lighting-implementation.md for full roadmap)
    // -----------------------------------------------------------------
    vec3 V = normalize(pc.cameraPos.xyz - inWorldPos);  // Phase 1: real camera position for correct view vector
    vec3 F0 = mix(vec3(0.04), albedo.rgb, sampledMetal);

    vec3 color = vec3(0.0);

    // Light 0 — original overhead (warm key)
    {
        vec3 L = LIGHT_DIR;
        vec3 H = normalize(L + V);
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);

        float D = D_GGX(NdotH, sampledRough);
        vec3  F = F_Schlick(LdotH, F0);
        float G = G_SmithGGX(NdotV, NdotL, sampledRough);

        vec3 spec = (F * D * G) / max(4.0 * NdotV * NdotL, 0.0001);
        vec3 kD = (1.0 - F) * (1.0 - sampledMetal);
        vec3 diff = kD * albedo.rgb / PI * NdotL;

        color += (diff + spec) * LIGHT_COLOR * LIGHT_INTENSITY;
    }

    // Light 1 — Phase 1 demo fill/rim (cooler, from upper-right)
    {
        vec3 L = normalize(vec3(0.6, -0.4, 0.7));
        vec3 Lc = vec3(0.6, 0.75, 0.95);
        float Li = 0.45;

        vec3 H = normalize(L + V);
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);

        float D = D_GGX(NdotH, sampledRough);
        vec3  F = F_Schlick(LdotH, F0);
        float G = G_SmithGGX(NdotV, NdotL, sampledRough);

        vec3 spec = (F * D * G) / max(4.0 * NdotV * NdotL, 0.0001);
        vec3 kD = (1.0 - F) * (1.0 - sampledMetal);
        vec3 diff = kD * albedo.rgb / PI * NdotL;

        color += (diff + spec) * Lc * Li;
    }

    // Ambient (Phase 1 improved constant + subtle tint)
    color += albedo.rgb * AMBIENT * AMBIENT_TINT;

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

    // === DEBUG VISUALIZATION (controlled via push constant extra.y) ===
    // Change the value in Engine.Render.cpp (look for "debugMode")
    // 0 = Normal rendering
    // 1 = UV visualization (Red = U, Green = V)
    // 2+ = Per-primitive/section color (each draw call gets a different color)
    uint debugMode = pc.extra.y;

    if (debugMode == 1) {
        // UV debug
        outColor = vec4(inUV, 0.0, 1.0);
    } else if (debugMode >= 2) {
        // Per-primitive color using the value in extra.y as an ID
        // This helps see if each draw call / primitive has constant UVs
        float id = float(debugMode % 8);
        vec3 col = vec3(id / 7.0);
        outColor = vec4(col, 1.0);
    }
    // else: normal rendering (debugMode == 0)
}