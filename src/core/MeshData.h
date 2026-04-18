#pragma once

#include <vector>
#include <cstdint>
#include "AABB.h"

namespace core {
/**
 * @brief CPU-side mesh data structure.
 * Used by GltfLoader to parse GLTF models and SceneManager to manage scene objects.
 */
struct MeshData {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<uint32_t> indices;
    core::AABB local_aabb;
};
}; // namespace core