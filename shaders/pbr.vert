#version 460

// Instanced PBR: viewProj in push constants.
// Per-instance model + material from DrawInstanceGPU SSBO (binding 1).
//
// Multi-draw: each indirect command sets firstInstance = batch region base.
// On Vulkan, gl_InstanceIndex ALREADY includes firstInstance
// (SPIR-V InstanceIndex = BaseInstance + InstanceId). Do NOT also add
// gl_BaseInstance or push.extra.x — that double-counts and pulls wrong
// instances/materials (and can index past packed regions → missing meshes).
layout(push_constant) uniform PushConstants {
    mat4   viewProj;
    uvec4  extra; // reserved
} pc;

struct DrawInstance {
    mat4  model;
    uvec4 meta; // x = material_index
};

layout(set = 0, binding = 1) readonly buffer DrawInstances {
    DrawInstance draw_instances[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUVPacked;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outTangent;
layout(location = 3) out vec2 outUV;
layout(location = 4) flat out uint outMaterialIndex;

void main() {
    uint uvPacked = floatBitsToUint(inUVPacked.x);
    vec2 uv;
    uv.x = float((uvPacked >> 0) & 0xFFFFu) / 65535.0;
    uv.y = float((uvPacked >> 16) & 0xFFFFu) / 65535.0;

    uint inst_id = uint(gl_InstanceIndex);
    DrawInstance inst = draw_instances[inst_id];
    mat4 modelMat = inst.model;
    outMaterialIndex = inst.meta.x;

    vec4 worldPos = modelMat * vec4(inPosition, 1.0);

    outWorldPos = worldPos.xyz;
    outNormal   = normalize(mat3(modelMat) * inNormal);
    outTangent  = vec4(normalize(mat3(modelMat) * inTangent.xyz), inTangent.w);
    outUV       = uv;

    gl_Position = pc.viewProj * worldPos;
}
