#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace gfx {

// Compact per-instance record for GPU instancing (std430-friendly).
// One contiguous array; each DrawBatch references [first_instance, first_instance + count).
struct DrawInstanceGPU {
    glm::mat4 model{1.0f};
    glm::uvec4 meta{0}; // x = material_index
};
static_assert(sizeof(DrawInstanceGPU) == 80, "DrawInstanceGPU must be 80 bytes");

// Static mesh draw template (geometry range). Visibility is decided per frame.
struct MeshDrawInfo {
    uint32_t mesh_index = 0;
    uint32_t index_count = 0;
    uint32_t index_offset = 0;
    int32_t  vertex_offset = 0;
    std::vector<uint32_t> render_mesh_ids; // SceneManager RenderMesh indices
};

// One multi-instance draw after culling (matches VkDrawIndexedIndirectCommand layout).
struct DrawBatch {
    uint32_t mesh_index = 0;
    uint32_t index_count = 0;
    uint32_t index_offset = 0;
    int32_t  vertex_offset = 0;
    uint32_t first_instance = 0; // gl_BaseInstance into DrawInstanceGPU[]
    uint32_t instance_count = 0;
};

// Push constants: viewProj only. Batch base is indirect firstInstance;
// VS reads gl_InstanceIndex (Vulkan already adds firstInstance — do not add again).
struct PbrPushInstanced {
    glm::mat4 viewProj{1.0f};
    glm::uvec4 extra{0}; // reserved
};
static_assert(sizeof(PbrPushInstanced) == 80, "PbrPushInstanced size");

} // namespace gfx
