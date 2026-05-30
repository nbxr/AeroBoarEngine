#version 450

// Push constants (Phase 1)
layout(push_constant) uniform PushConstants {
    mat4   viewProj;
    mat4   model;
    uvec4  extra;   // x = materialIndex
} pc;

// Bindless resources (same as debug_draw.vert)
layout(set = 0, binding = 1) readonly buffer SceneInstances {
    uint material_index;
    uint mesh_index;
    uint flags;
    mat4 transform;
} scene_instances[];

layout(set = 0, binding = 4) readonly buffer VertexBuffer {
    float data[];
} vertices;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outTangent;
layout(location = 3) out vec2 outUV;

void main() {
    // Vertex stride is 56 bytes = 14 floats (matches gfx::Vertex)
    uint base = gl_VertexIndex * 14;

    vec3 localPos = vec3(
        vertices.data[base + 0],
        vertices.data[base + 1],
        vertices.data[base + 2]
    );

    vec3 localNormal = vec3(
        vertices.data[base + 3],
        vertices.data[base + 4],
        vertices.data[base + 5]
    );

    vec4 localTangent = vec4(
        vertices.data[base + 6],
        vertices.data[base + 7],
        vertices.data[base + 8],
        vertices.data[base + 9]
    );

    // Unpack UV0 (stored as two uint16 packed into uint8[4])
    uint uvPacked = floatBitsToUint(vertices.data[base + 10]); // first 4 bytes of uv
    vec2 uv;
    uv.x = float((uvPacked >> 0) & 0xFFFFu) / 65535.0;
    uv.y = float((uvPacked >> 16) & 0xFFFFu) / 65535.0;

    // glTF UVs are often top-left origin; flip V for correct texture orientation
    // in Vulkan/OpenGL renderers. This fixes many "texture looks wrong" cases.
    uv.y = 1.0 - uv.y;

    // Use the model matrix pushed per draw (Phase 1)
    mat4 modelMat = pc.model;

    vec4 worldPos = modelMat * vec4(localPos, 1.0);

    outWorldPos = worldPos.xyz;
    outNormal   = normalize(mat3(modelMat) * localNormal);
    outTangent  = vec4(normalize(mat3(modelMat) * localTangent.xyz), localTangent.w);
    outUV       = uv;

    gl_Position = pc.viewProj * worldPos;
}