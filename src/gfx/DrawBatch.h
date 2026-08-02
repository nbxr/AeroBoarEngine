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

// One multi-instance draw: all instances share the same mesh primitive (index/vertex range).
struct DrawBatch {
    uint32_t mesh_index = 0;
    uint32_t index_count = 0;
    uint32_t index_offset = 0;  // firstIndex for vkCmdDrawIndexed
    int32_t  vertex_offset = 0; // vertexOffset for vkCmdDrawIndexed
    uint32_t first_instance = 0;
    uint32_t instance_count = 0;
};

// Push constants for instanced PBR (viewProj + base instance index).
// Model/material come from DrawInstanceGPU[base + gl_InstanceIndex].
struct PbrPushInstanced {
    glm::mat4 viewProj{1.0f};
    glm::uvec4 extra{0}; // x = first_instance into DrawInstanceGPU[]
                         // y = debugMode (0 = normal)
};
static_assert(sizeof(PbrPushInstanced) == 80, "PbrPushInstanced size");

} // namespace gfx
