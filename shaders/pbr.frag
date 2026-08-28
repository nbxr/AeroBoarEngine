#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Materials SSBO (binding 2): single buffer containing a runtime array of Material.
// C++ binds one STORAGE_BUFFER with material_count * sizeof(Material) packed contiguously.
// Do NOT use "} materials[];" — that is an array of buffer descriptors (descriptorCount > 1),
// not an array of elements inside one buffer. Accessing materials[i] for i >= 1 with only one
// descriptor bound is invalid and commonly causes AMD DEVICE_LOST when fragments run.
// Layout must match gfx::Material (std430, 256-byte stride).
struct Material {
    vec4  albedo;
    float roughness;
    float metallic;
    float normalStrength;
    float clearcoat;
    vec4  emissive_factor; // rgb + strength (.w)
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

// IBL: prefiltered specular env + BRDF LUT (engine-owned, not bindless array)
layout(set = 0, binding = 7) uniform samplerCube prefilteredEnv;
layout(set = 0, binding = 8) uniform sampler2D brdfLut;

layout(set = 0, binding = 10) uniform sampler2DShadow shadowMap;
// Bindless textures — must be highest binding (VARIABLE_DESCRIPTOR_COUNT).
layout(set = 0, binding = 11) uniform sampler2D bindlessTextures[];

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUV0;
layout(location = 4) in vec2 inUV1;
layout(location = 5) flat in uint inMaterialIndex;
layout(location = 6) in vec4 inColor; // glTF COLOR_0 (linear unorm)

// Push constants (must match pbr.vert / gfx::PbrPushInstanced)
layout(push_constant) uniform PushConstants {
    mat4   viewProj;
    uvec4  extra; // reserved
} pc;

#ifdef WBOIT
layout(location = 0) out vec4 outAccum;
layout(location = 1) out float outReveal;
#else
layout(location = 0) out vec4 outColor;
#endif

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
    mat4  shadowViewProj;
    vec4  shadowParams;     // x=texel UV, y=enabled, z=light index, w=bias
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

// KHR_texture_transform: T * R * S applied as mat3 * vec3(uv, 1)
// scale_offset = vec4(scale.xy, offset.xy)
vec2 apply_uv_transform(vec2 uv, vec4 scale_offset, float rotation) {
    float c = cos(rotation);
    float s = sin(rotation);
    vec2 scaled = uv * scale_offset.xy;
    vec2 rotated = vec2(c * scaled.x - s * scaled.y, s * scaled.x + c * scaled.y);
    return rotated + scale_offset.zw;
}

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

// Simplified iridescence: thin-film hue shift mixed into F0 (not full spectral BRDF).
vec3 iridescence_f0(vec3 baseF0, float factor, float ior, float thickness_nm, float NdotV) {
    if (factor < 1e-4) return baseF0;
    // Optical path proxy — produces view-dependent rainbow tint for car pearl paint.
    float phase = thickness_nm * 0.01 * (ior + NdotV);
    vec3 film = 0.5 + 0.5 * cos(vec3(phase, phase + 2.094, phase + 4.189));
    return mix(baseF0, clamp(film, 0.0, 1.0), clamp(factor, 0.0, 1.0));
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

// Reverse-Z directional map: COMPARE_OP_GREATER_OR_EQUAL.
// 8-tap Vogel PCF (~1.25 texels): anti-aliases the silhouette without a fat penumbra.
// LINEAR compare still does a 2x2 filter per tap. Cheap enough for stereo later.
float sample_shadow(vec3 worldPos) {
    if (globals.shadowParams.y < 0.5)
        return 1.0;
    vec4 sc = globals.shadowViewProj * vec4(worldPos, 1.0);
    sc.xyz /= max(sc.w, 1e-6);
    vec2 uv = sc.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0;
    float z = sc.z + globals.shadowParams.w;
    float t = max(globals.shadowParams.x, 1e-6) * 1.25;
    // Vogel disk (golden-angle). Fixed offsets, uniform across the wave.
    const vec2 kOff[8] = vec2[](
        vec2( 0.1250,  0.0000),
        vec2(-0.1585,  0.1970),
        vec2(-0.0644, -0.3369),
        vec2( 0.3552,  0.1298),
        vec2(-0.3543,  0.2573),
        vec2( 0.0329, -0.4989),
        vec2( 0.4250,  0.3492),
        vec2(-0.5688, -0.0825)
    );
    float s = 0.0;
    for (int i = 0; i < 8; ++i)
        s += texture(shadowMap, vec3(uv + kOff[i] * t, z));
    return s * 0.125;
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
    float clearcoat   = materials[matIdx].clearcoat;
    float clearcoatR  = materials[matIdx].clearcoat_roughness;
    float transmission = materials[matIdx].transmission;
    float iridescence  = materials[matIdx].iridescence;

    #define UV_FOR(slot)                                                          \
        apply_uv_transform(                                                       \
            (((materials[matIdx].texcoord_packed >> ((slot) * 4u)) & 0xFu) == 0u)\
                ? inUV0                                                           \
                : inUV1,                                                          \
            materials[matIdx].uv_scale_offset[slot],                              \
            materials[matIdx].uv_rotation[slot])

    // Sample albedo (baseColorFactor * baseColorTexture * COLOR_0)
    vec4 albedo = baseColor;
    if (albedoIdx != NO_TEXTURE) {
        albedo *= texture(bindlessTextures[nonuniformEXT(albedoIdx)], UV_FOR(0u));
    }
    albedo *= inColor;

    const uint matFlags = materials[matIdx].flags;
    const float alphaCutoff = materials[matIdx].alpha_cutoff;
    if ((matFlags & 0x80u) != 0u) { // MASK
        if (albedo.a < alphaCutoff)
            discard;
        albedo.a = 1.0;
    } else if ((matFlags & 0x20u) == 0u) {
        albedo.a = 1.0;
    }
    // Transmission: leave some alpha for blend path (MVP glass)
    if ((matFlags & 0x400u) != 0u) { // transmission
        albedo.a = mix(albedo.a, 0.15, clamp(transmission, 0.0, 1.0));
    }

    // metallicRoughness texture * factors (glTF multiplies)
    float sampledRough = roughness;
    float sampledMetal = metallic;
    if (ormIdx != NO_TEXTURE) {
        vec3 orm = texture(bindlessTextures[nonuniformEXT(ormIdx)], UV_FOR(2u)).rgb;
        sampledRough *= orm.g;
        sampledMetal *= orm.b;
    }

    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent.xyz);
    vec3 B = normalize(cross(N, T) * inTangent.w);
    N = getNormalFromMap(N, T, B, UV_FOR(1u), normalIdx, normalStr, matFlags);

    float exposure = globals.cameraPosition.w;
    vec3 V = normalize(globals.cameraPosition.xyz - inWorldPos);
    float NdotV = max(dot(N, V), 0.001);
    vec3 F0 = mix(vec3(0.04), albedo.rgb, sampledMetal);
    if ((matFlags & 0x800u) != 0u) {
        F0 = iridescence_f0(F0, iridescence, materials[matIdx].iridescence_ior,
                            materials[matIdx].iridescence_thickness, NdotV);
    }

    // Transmission: reduce base diffuse (energy leaves the surface)
    float opaque = 1.0 - clamp(transmission, 0.0, 1.0);
    vec3 color = vec3(0.0);

    uint numLights = min(globals.lightMeta.x, MAX_LIGHTS);

    for (uint i = 0u; i < numLights; ++i) {
        GpuLight light = lights[i];
        uint lightType = uint(light.params.x + 0.5);

        vec3 L;
        float attenuation = 1.0;

        if (lightType == 0u) {
            L = normalize(light.direction.xyz);
        } else {
            vec3 lightPos = light.position.xyz;
            vec3 toLight = lightPos - inWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-4);

            float range = light.params.y;
            if (range > 0.0) {
                float x = max(0.0, 1.0 - (dist / range));
                attenuation = x * x;
            } else {
                attenuation = 1.0 / max(dist * dist, 1e-4);
            }

            if (lightType == 2u) {
                vec3 D = normalize(light.direction.xyz);
                float cosTheta = dot(-L, D);
                float cosInner = light.params.z;
                float cosOuter = light.params.w;
                attenuation *= smoothstep(cosOuter, cosInner, cosTheta);
            }
        }

        vec3 H = normalize(L + V);
        float NdotL = max(dot(N, L), 0.0);
        float NdotH = max(dot(N, H), 0.0);
        float LdotH = max(dot(L, H), 0.0);

        if (NdotL <= 0.0) continue;

        float D = D_GGX(NdotH, sampledRough);
        vec3  F = F_Schlick(LdotH, F0);
        float G = G_SmithGGX(NdotV, NdotL, sampledRough);

        vec3 spec = (F * D * G) / max(4.0 * NdotV * NdotL, 0.0001);
        vec3 kD = (1.0 - F) * (1.0 - sampledMetal) * opaque;
        vec3 diff = kD * albedo.rgb / PI * NdotL;

        vec3 lightColor = light.colorIntensity.rgb;
        float intensity = light.colorIntensity.a;
        float lightScale = intensity * attenuation * exposure;
        if (globals.shadowParams.y > 0.5 &&
            i == uint(globals.shadowParams.z + 0.5))
            lightScale *= sample_shadow(inWorldPos);

        color += (diff + spec) * lightColor * lightScale;

        // Clearcoat: second specular lobe (car paint)
        if ((matFlags & 0x200u) != 0u && clearcoat > 1e-4) {
            float NcL = NdotL;
            float NcV = NdotV;
            float NcH = NdotH;
            float Dc = D_GGX(NcH, clearcoatR);
            float Fc = 0.04 + (1.0 - 0.04) * pow(1.0 - LdotH, 5.0);
            float Gc = G_SmithGGX(NcV, NcL, clearcoatR);
            float clearSpec = (Fc * Dc * Gc) / max(4.0 * NcV * NcL, 0.0001);
            color += vec3(clearSpec) * clearcoat * lightColor * lightScale;
        }
    }

    // Ambient occlusion (glTF): multiplies *indirect* lighting only.
    // Applying AO to the full final color crushed emissive maps (Dash_E, Khronos_C
    // logos on Hardware/Dashboard/Rims) and darkened direct light incorrectly.
    float ao = 1.0;
    if (aoIdx != NO_TEXTURE) {
        ao = texture(bindlessTextures[nonuniformEXT(aoIdx)], UV_FOR(4u)).r;
    }

    // Diffuse IBL
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
    color += albedo.rgb * irradiance * (1.0 - sampledMetal) * opaque * ao;

    // Specular IBL
    if (globals.iblIndices.x != 0u && globals.iblIndices.y != 0u) {
        vec3 R = reflect(-V, N);
        const float maxMip = 3.0;
        float mip = sampledRough * maxMip;
        vec3 prefiltered = textureLod(prefilteredEnv, R, mip).rgb;
        vec2 brdf = texture(brdfLut, vec2(NdotV, sampledRough)).rg;
        vec3 specularIBL = prefiltered * (F0 * brdf.x + brdf.y);
        color += specularIBL * exposure * mix(0.35, 1.0, sampledMetal) * ao;

        // Clearcoat env lobe
        if ((matFlags & 0x200u) != 0u && clearcoat > 1e-4) {
            float mipC = clearcoatR * maxMip;
            vec3 prefC = textureLod(prefilteredEnv, R, mipC).rgb;
            float Fc = 0.04 + (1.0 - 0.04) * pow(1.0 - NdotV, 5.0);
            color += prefC * Fc * clearcoat * exposure * ao;
        }

        // Transmission MVP: add env along view refraction proxy
        if ((matFlags & 0x400u) != 0u && transmission > 1e-4) {
            vec3 Rt = refract(-V, N, 1.0 / 1.5);
            if (dot(Rt, Rt) < 1e-6)
                Rt = -V;
            vec3 transEnv = textureLod(prefilteredEnv, Rt, sampledRough * maxMip).rgb;
            color += transEnv * albedo.rgb * transmission * exposure * ao;
        }
    }

    // Emissive: factor.rgb * strength * texture (not affected by AO)
    vec3 emissiveRgb = materials[matIdx].emissive_factor.rgb *
                       materials[matIdx].emissive_factor.w;
    if (emissiveIdx != NO_TEXTURE) {
        emissiveRgb *=
            texture(bindlessTextures[nonuniformEXT(emissiveIdx)], UV_FOR(3u)).rgb;
    }
    // Clearcoat attenuates emission slightly (KHR note)
    if ((matFlags & 0x200u) != 0u && clearcoat > 1e-4) {
        float Fc = 0.04 + (1.0 - 0.04) * pow(1.0 - NdotV, 5.0);
        emissiveRgb *= (1.0 - clearcoat * Fc);
    }
    color += emissiveRgb;

#ifdef WBOIT
    // Weighted blended OIT (McGuire). Reverse-Z: window z is 1=near; convert
    // to a standard-Z-like value for the published weight curve.
    float a = clamp(albedo.a, 0.0, 1.0);
    float z = clamp(1.0 - gl_FragCoord.z, 0.0, 1.0);
    float w = clamp(pow(min(1.0, a * 10.0) + 0.01, 3.0) * 1e8 *
                        pow(max(1e-3, 1.0 - z * 0.9), 3.0),
                    1e-2, 3e3);
    outAccum = vec4(color * a, a) * w;
    outReveal = a;
#else
    color = color / (color + 1.0);
    outColor = vec4(color, albedo.a);
#endif
}