#pragma once
#include <glm/glm.hpp>
#include "AABB.h"

namespace core {
/**
 * @brief Flat data descriptor for a single instance in the scene.
 * Matches the GPU-friendly SOA layout for efficient culling and rendering.
 */
struct SceneInstance {
    uint32_t material_index; // Index into global material SSBO/bindless array
    uint32_t mesh_index;     // Index into global mesh SSBO/bindless array
    uint32_t flags;          // Reserved for future use
    glm::mat4 transform;     // Column-major 4x4 matrix
    core::AABB local_aabb;
};
};