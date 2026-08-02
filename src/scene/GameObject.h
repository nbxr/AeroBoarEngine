#pragma once

#include <cstdint>

namespace scene {

// High-level entity. Owns a root transform and zero or more RenderMeshes.
// Game logic (health, AI, etc.) will hang off this later.
struct GameObject {
    uint32_t root_transform_index = ~0u;
    uint32_t skin_index = ~0u; // shared skeleton if any

    // RenderMesh ownership: contiguous range in SceneManager::render_meshes
    // (filled at load). Dynamic spawn may use different bookkeeping later.
    uint32_t first_render_mesh = ~0u;
    uint32_t render_mesh_count = 0;

    uint32_t flags = 0;
    uint32_t gltf_node_index = ~0u; // debug / tooling
};

} // namespace scene
