#version 460

// Instanced PBR: viewProj in push constants.
// Per-instance model + material + optional skin from DrawInstanceGPU SSBO.
//
// Multi-draw: each indirect command sets firstInstance = batch region base.
// On Vulkan, gl_InstanceIndex ALREADY includes firstInstance.
// Skinning (glTF): world = model * sum(w_i * jointMatrix[joint_i]) * pos
//   jointMatrix = inv(meshWorld) * jointWorld * IBM  (uploaded CPU-side)
//   model       = mesh node world (DrawInstance.model)
// Rigid: world = model * pos

layout(push_constant) uniform PushConstants {
    mat4   viewProj;
    uvec4  extra; // reserved
} pc;

struct DrawInstance {
    mat4  model;
    uvec4 meta; // x = material, y = joint_base, z = joint_count (0 = rigid)
};

layout(set = 0, binding = 1) readonly buffer DrawInstances {
    DrawInstance draw_instances[];
};

// Packed joint matrices (inv(meshWorld) * jointWorld * IBM) for all skins.
layout(set = 0, binding = 9) readonly buffer JointMatrices {
    mat4 joint_matrices[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUVPacked;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outTangent;
layout(location = 3) out vec2 outUV;
layout(location = 4) flat out uint outMaterialIndex;

mat4 skin_matrix(uint joint_base, uvec4 joints, vec4 weights) {
    // Normalize weights (glTF may not sum exactly to 1).
    float wsum = weights.x + weights.y + weights.z + weights.w;
    vec4 w = (wsum > 1e-6) ? (weights / wsum) : vec4(1.0, 0.0, 0.0, 0.0);
    mat4 m =
        w.x * joint_matrices[joint_base + joints.x] +
        w.y * joint_matrices[joint_base + joints.y] +
        w.z * joint_matrices[joint_base + joints.z] +
        w.w * joint_matrices[joint_base + joints.w];
    return m;
}

void main() {
    uint uvPacked = floatBitsToUint(inUVPacked.x);
    vec2 uv;
    uv.x = float((uvPacked >> 0) & 0xFFFFu) / 65535.0;
    uv.y = float((uvPacked >> 16) & 0xFFFFu) / 65535.0;

    uint inst_id = uint(gl_InstanceIndex);
    DrawInstance inst = draw_instances[inst_id];
    outMaterialIndex = inst.meta.x;

    mat4 modelMat = inst.model;
    uint joint_count = inst.meta.z;
    if (joint_count > 0u) {
        // Mesh-relative skin, then mesh world (inst.model).
        modelMat = inst.model * skin_matrix(inst.meta.y, inJoints, inWeights);
    }

    vec4 worldPos = modelMat * vec4(inPosition, 1.0);

    outWorldPos = worldPos.xyz;
    outNormal   = normalize(mat3(modelMat) * inNormal);
    outTangent  = vec4(normalize(mat3(modelMat) * inTangent.xyz), inTangent.w);
    outUV       = uv;

    gl_Position = pc.viewProj * worldPos;
}
