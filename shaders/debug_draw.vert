#version 450

// === Push constants ===
layout(push_constant) uniform PushConstants {
    mat4  viewProj;
    mat4  model;   // per-draw model transform for this mesh primitive
} pc;

// Scene instances (binding 1): single SSBO with a runtime array of instances.
// Matches C++ packing (one STORAGE_BUFFER), not an array of buffer descriptors.
struct SceneInstanceGPU {
    uint material_index;
    uint mesh_index;
    uint flags;
    mat4 transform;
    // AABB follows in C++ SceneInstance; omitted here (debug path ignores it)
};

layout(set = 0, binding = 1) readonly buffer SceneInstances {
    SceneInstanceGPU scene_instances[];
};

// Mesh primitive metadata (binding 3): single SSBO, runtime array of primitives.
struct MeshPrimitiveGPU {
    uint vertex_offset;
    uint vertex_count;
    uint index_offset;
    uint index_count;
};

layout(set = 0, binding = 3) readonly buffer MeshPrimitives {
    MeshPrimitiveGPU meshes[];
};

// Raw vertex buffer (positions only for debug)
layout(set = 0, binding = 4) readonly buffer VertexBuffer {
    float data[];
} vertices;

layout(location = 0) out vec3 outColor;

void main() {
    // gl_VertexIndex is already biased by the vertexOffset passed to vkCmdDrawIndexed,
    // so it directly indexes the global vertex in the big buffer for this primitive.
    uint vtx = gl_VertexIndex;

    // Vertex stride is 56 bytes = 14 floats (matches gfx::Vertex)
    // Position is the first 3 floats of each vertex
    vec3 pos = vec3(
        vertices.data[vtx * 14 + 0],
        vertices.data[vtx * 14 + 1],
        vertices.data[vtx * 14 + 2]
    );

    gl_Position = pc.viewProj * pc.model * vec4(pos, 1.0);

    // === TEMPORARY DEBUG HACK ===
    // Force depth to middle of range so depth test can't reject us while we
    // validate geometry. Remove once real vertex data produces correct depth.
    // TODO: remove forced z and magenta clear after geometry is reliably visible.
    gl_Position.z = 0.5;

    outColor = vec3(0.2, 0.9, 0.3);
}
