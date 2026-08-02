#pragma once

#include "core/AABB.h"
#include <cstdint>

namespace scene {

// Thin render descriptor: one entry maps 1:1 to a draw / future cull item.
// Transform is an index into TransformManager (root or bone-derived later).
struct RenderMesh {
    uint32_t game_object_index = ~0u;
    uint32_t mesh_index = ~0u;      // MeshManager primitive id
    uint32_t material_index = ~0u;  // MaterialManager id
    uint32_t transform_index = ~0u; // TransformManager id
    uint32_t skin_index = ~0u;      // ~0 = rigid
    core::AABB local_aabb{};        // mesh-local (not world)
    uint32_t flags = 0;
};

} // namespace scene
