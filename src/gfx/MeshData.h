#pragma once

#include <vector>
#include <cstdint>
#include "core/AABB.h"
#include "core/Handle.h"
#include "gfx/Vertex.h"
#include <glm/glm.hpp>

namespace gfx {
/**
 * @brief CPU-side mesh data structure.
 * Used by GltfLoader to parse GLTF models and SceneManager to manage scene objects.
 */
// GPU/CPU meshlet (std430, 64 B). first_index is relative to the primitive IB.
struct MeshletDesc {
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    glm::vec4 cone_apex_cutoff{0.0f}; // xyz apex, w = cos(angle/2)
    glm::vec4 cone_axis_radius{0.0f}; // xyz axis, w = sphere radius
    glm::vec4 sphere_center{0.0f};    // xyz object-space center
};
static_assert(sizeof(MeshletDesc) == 64, "MeshletDesc size");

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<Index> indices;
    core::AABB local_aabb;
    // Transient: old→new vertex map after load-time optimize. Consumed by
    // MorphSystem (must follow the same remap). Empty = identity / unused.
    std::vector<uint32_t> vertex_remap;
    std::vector<MeshletDesc> meshlets;
    // GPU meshlet cone cull is bind-pose; skip for skin / morph.
    bool allow_meshlet_cull = true;
};
} // namespace gfx

// Mesh primitive ID (in gfx namespace)
namespace gfx {
using MeshPrimitiveID = core::Handle<MeshData>;
} // namespace gfx