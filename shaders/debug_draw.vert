#version 450

// === Push constants (used for camera for now) ===
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

// Scene instances (binding 1) - array of all instances in the scene
layout(set = 0, binding = 1) readonly buffer SceneInstances {
    uint material_index;
    uint mesh_index;
    uint flags;
    mat4 transform;
    // AABB follows (ignored for drawing)
} scene_instances[];

// Mesh primitive metadata (binding 3) - we can use this later for per-mesh drawing
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
    uint vtx = meshes.vertex_offset + gl_VertexIndex;

    // Vertex stride is 56 bytes = 14 floats
    // Position is the first 3 floats of each vertex
    vec3 pos = vec3(
        vertices.data[vtx * 14 + 0],
        vertices.data[vtx * 14 + 1],
        vertices.data[vtx * 14 + 2]
    );

    mat4 model = scene_instances[0].transform;
    gl_Position = pc.viewProj * model * vec4(pos, 1.0);

    outColor = vec3(0.2, 0.9, 0.3);
}
