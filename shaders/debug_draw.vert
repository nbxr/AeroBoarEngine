#version 450

// === Push constants (used for camera for now) ===
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

// === Bindless scene data ===
layout(set = 0, binding = 1) readonly buffer SceneInstances {
    uint material_index;
    uint mesh_index;
    uint flags;
    mat4 transform;
    // AABB follows (we ignore for drawing)
} instances;

layout(set = 0, binding = 3) readonly buffer MeshPrimitives {
    uint vertex_offset;
    uint vertex_count;
    uint index_offset;
    uint index_count;
} meshes;

// Raw vertex buffer (positions only for debug)
layout(set = 0, binding = 4) readonly buffer VertexBuffer {
    float data[];
} vertices;

layout(location = 0) out vec3 outColor;

void main() {
    // Debug: Draw the first mesh primitive of the first instance
    // (we'll make this more general soon)
    uint vtx = meshes.vertex_offset + gl_VertexIndex;

    // Vertex stride is 56 bytes = 14 floats
    // Position is at the start of each vertex
    vec3 pos = vec3(
        vertices.data[vtx * 14 + 0],
        vertices.data[vtx * 14 + 1],
        vertices.data[vtx * 14 + 2]
    );

    // Apply instance transform + camera
    gl_Position = pc.viewProj * instances.transform * vec4(pos, 1.0);

    // Debug color
    outColor = vec3(0.3, 0.6, 0.9);
}
