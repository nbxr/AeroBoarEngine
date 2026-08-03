#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Materials SSBO (binding 2): single buffer containing a runtime array of Material.
// C++ binds one STORAGE_BUFFER with material_count * sizeof(Material) packed contiguously.
// Do NOT use "} materials[];" — that is an array of buffer descriptors (descriptorCount > 1),
// not an array of elements inside one buffer. Accessing materials[i] for i >= 1 with only one
// descriptor bound is invalid and commonly causes AMD DEVICE_LOST when fragments run.
// Layout must match gfx::Material (std430, 80-byte stride with vec4 alignment).
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

// IBL: prefiltered specular env + BRDF LUT (engine-owned, not bindless array)
layout(set = 0, binding = 7) uniform samplerCube prefilteredEnv;
layout(set = 0, binding = 8) uniform sampler2D brdfLut;

// Bindless textures — must be highest binding (VARIABLE_DESCRIPTOR_COUNT).
layout(set = 0, binding = 9) uniform sampler2D bindlessTextures[];

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV;
layout(location = 4) flat in uint inMaterialIndex;

// Push constants (must match pbr.vert / gfx::PbrPushInstanced)
layout(push_constant) uniform PushConstants {
    mat4   viewProj;
    uvec4  extra; // reserved
} pc;

layout(location = 0) out vec4 outColor;

// -----------------------------------------------------------------------------
// Lighting: FrameConstants UBO (binding 0) + Lights SSBO (binding 6).
// Scene KHR_lights_punctual when present, else engine global directional.
// See docs/architecture/lighting-implementation.md
// -----------------------------------------------------------------------------

const uint MAX_LIGHTS = 8; // matches gfx::MAX_LIGHTS

// std140-friendly: only vec4/uvec4 members (matches gfx::FrameConstants)
layout(set = 0, binding = 0) uniform FrameConstants {
    vec4  cameraPosition;   // xyz = world camera, w = exposure
    uvec4 lightMeta;        // x = lightCount
    vec4  shCoefficients[9];
    uvec4 iblIndices;       // x = specularEnvMapIndex, y = brdfLutIndex
} globals;

// Single SSBO + runtime array (std430). Matches gfx::GpuLight (64 bytes).
struct GpuLight {
    vec4 position;       // xyz = world pos (point/spot)
    vec4 direction;      // xyz = to-light (dir) or emission dir (spot)
    vec4 colorIntensity; // rgb + intensity
    vec4 params;         // x=type, y=range, z=cos(inner), w=cos(outer)
};

layout(set = 0, binding = 6) readonly buffer Lights {
    GpuLight lights[];
};

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
    // Per-instance material (flat interpolated from VS)
    uint matIdx = inMaterialIndex;

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
    // Lighting — data driven from FrameGlobals (scene KHR_lights_punctual or
    // engine global fallback) + proper GGX BRDF.
    // See docs/architecture/lighting-implementation.md
    // -----------------------------------------------------------------
    float exposure = globals.cameraPosition.w;
    vec3 V = normalize(globals.cameraPosition.xyz - inWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo.rgb, sampledMetal);

    vec3 color = vec3(0.0);

    uint numLights = min(globals.lightMeta.x, MAX_LIGHTS);

    for (uint i = 0u; i < numLights; ++i) {
        GpuLight light = lights[i];
        uint lightType = uint(light.params.x + 0.5); // 0=dir, 1=point, 2=spot

        vec3 L;
        float attenuation = 1.0;

        if (lightType == 0u) {
            // Directional: direction.xyz is to-light (L)
            L = normalize(light.direction.xyz);
        } else {
            // Point or Spot: position.xyz is world light origin
            vec3 lightPos = light.position.xyz;
            vec3 toLight = lightPos - inWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-4);

            float range = light.params.y;
            if (range > 0.0) {
                // Soft cutoff when range is authored
                float x = max(0.0, 1.0 - (dist / range));
                attenuation = x * x;
            } else {
                // KHR_lights_punctual: infinite range, inverse-square (candela)
                attenuation = 1.0 / max(dist * dist, 1e-4);
            }

            if (lightType == 2u) {
                // Spot: emission direction D (KHR −Z). Angle between D and
                // light→surface (−L). params.zw = cos(inner), cos(outer).
                vec3 D = normalize(light.direction.xyz);
                float cosTheta = dot(-L, D);
                float cosInner = light.params.z;
                float cosOuter = light.params.w;
                // Smooth falloff from full at inner to zero at outer.
                float spotAtten = smoothstep(cosOuter, cosInner, cosTheta);
                attenuation *= spotAtten;
            }
        }

        vec3 H = normalize(L + V);
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);

        if (NdotL <= 0.0) continue;

        float D = D_GGX(NdotH, sampledRough);
        vec3  F = F_Schlick(LdotH, F0);
        float G = G_SmithGGX(NdotV, NdotL, sampledRough);

        vec3 spec = (F * D * G) / max(4.0 * NdotV * NdotL, 0.0001);
        vec3 kD = (1.0 - F) * (1.0 - sampledMetal);
        vec3 diff = kD * albedo.rgb / PI * NdotL;

        vec3 lightColor = light.colorIntensity.rgb;
        float intensity = light.colorIntensity.a;

        color += (diff + spec) * lightColor * intensity * attenuation * exposure;
    }

    // -----------------------------------------------------------------
    // -----------------------------------------------------------------
    // Diffuse IBL: SH irradiance * albedo (Lambertian)
    // -----------------------------------------------------------------
    vec3 irradiance = vec3(0.0);
    if (globals.shCoefficients[0].x != 0.0 || globals.shCoefficients[0].y != 0.0 ||
        globals.shCoefficients[0].z != 0.0) {
        irradiance =
            globals.shCoefficients[0].rgb +
            globals.shCoefficients[1].rgb * N.y +
            globals.shCoefficients[2].rgb * N.z +
            globals.shCoefficients[3].rgb * N.x +
            globals.shCoefficients[4].rgb * N.x * N.y +
            globals.shCoefficients[5].rgb * N.y * N.z +
            globals.shCoefficients[6].rgb * (3.0 * N.z * N.z - 1.0) * 0.5 +
            globals.shCoefficients[7].rgb * N.z * N.x +
            globals.shCoefficients[8].rgb * (N.x * N.x - N.y * N.y);
        irradiance = max(irradiance, vec3(0.0));
    } else {
        irradiance = vec3(0.03) * vec3(0.95, 0.98, 1.05);
    }
    color += albedo.rgb * irradiance * (1.0 - sampledMetal);

    // -----------------------------------------------------------------
    // Specular IBL: split-sum (prefiltered env * (F0*brdf.x + brdf.y))
    // Enabled when iblIndices.x/y are non-zero (set when IblEnvironment is ready).
    // -----------------------------------------------------------------
    if (globals.iblIndices.x != 0u && globals.iblIndices.y != 0u) {
        vec3 R = reflect(-V, N);
        float NdotV_ibl = max(dot(N, V), 0.001);
        // Mip count is fixed by the baker (face 32 → ~4 mips). Keep in sync with
        // IblEnvironment::kCubeSize / mip generation.
        const float maxMip = 3.0;
        float mip = sampledRough * maxMip;
        vec3 prefiltered = textureLod(prefilteredEnv, R, mip).rgb;
        vec2 brdf = texture(brdfLut, vec2(NdotV_ibl, sampledRough)).rg;
        vec3 specularIBL = prefiltered * (F0 * brdf.x + brdf.y);
        // Rough metals keep more env; dielectrics get a smaller Fresnel-weighted share.
        color += specularIBL * exposure * mix(0.35, 1.0, sampledMetal);
    }

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

    // Simple compressor so high-intensity scene lights (after exposure scaling)
    // and any bright emissive produce visible shading instead of hard white.
    // (A full tonemapper + gamma would go here in a later phase.)
    color = color / (color + 1.0);

    outColor = vec4(color, albedo.a);
}