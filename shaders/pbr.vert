#version 450

// Instanced PBR: viewProj + base instance index in push constants.
// Per-instance model + material from DrawInstanceGPU SSBO (binding 1).
layout(push_constant) uniform PushConstants {
    mat4   viewProj;
    uvec4  extra; // x = first_instance into draw_instances[]
} pc;

struct DrawInstance {
    mat4  model;
    uvec4 meta; // x = material_index
};

layout(set = 0, binding = 1) readonly buffer DrawInstances {
    DrawInstance draw_instances[];
};

// Vertex attributes
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

    uint inst_id = pc.extra.x + uint(gl_InstanceIndex);
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
