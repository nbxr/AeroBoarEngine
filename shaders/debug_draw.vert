#version 450

// Bindings match our global bindless descriptor set
layout(set = 0, binding = 1) readonly buffer SceneInstances {
    // Simplified: we only read the first instance for now
    uint material_index;
    uint mesh_index;
    uint flags;
    mat4 transform;
    // AABB would follow but we ignore it for drawing
} instances;

layout(set = 0, binding = 3) readonly buffer MeshPrimitives {
    uint vertex_offset;
    uint vertex_count;
    uint index_offset;
    uint index_count;
} meshes;

layout(set = 0, binding = 4) readonly buffer Vertices {
    // We only care about position for the first test
    float data[];   // interleaved, but we'll stride manually
} vertices;

layout(location = 0) out vec3 outColor;

void main() {
    // For the very first test draw, draw the first triangle of the first mesh
    // using gl_VertexIndex (0,1,2)
    uint prim = 0; // first mesh primitive
    uint idx = meshes.index_offset + gl_VertexIndex;
    
    // Very crude: assume indices are tightly packed after index_offset in the index buffer
    // For now we do direct vertex pulling without the index buffer to keep it simple
    uint vtx = meshes.vertex_offset + gl_VertexIndex;

    // Vertex stride = 56 bytes (from Vertex.h)
    // position is at offset 0
    vec3 pos = vec3(
        vertices.data[vtx * 14 + 0],
        vertices.data[vtx * 14 + 1],
        vertices.data[vtx * 14 + 2]
    );

    gl_Position = instances.transform * vec4(pos, 1.0);
    
    // Simple color based on vertex index for debugging
    outColor = vec3(0.2 + float(gl_VertexIndex) * 0.3, 
                    0.4, 
                    0.6 + float(gl_VertexIndex) * 0.1);
}
