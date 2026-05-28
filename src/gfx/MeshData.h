#pragma once

#include <vector>
#include <cstdint>
#include "core/AABB.h"
#include "core/Handle.h"
#include "Vertex.h"

namespace gfx {
/**
 * @brief CPU-side mesh data structure.
 * Used by GltfLoader to parse GLTF models and SceneManager to manage scene objects.
 */
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<Index> indices;
    core::AABB local_aabb;
};
} // namespace gfx

// Mesh primitive ID (in gfx namespace)
namespace gfx {
using MeshPrimitiveID = core::Handle<MeshData>;
} // namespace gfx