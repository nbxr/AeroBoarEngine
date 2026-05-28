#pragma once
#include "core/AABB.h"
#include "core/Handle.h"

namespace scene {
struct RenderMesh {
    core::Handle<GameObject> game_object_ID;
    uint32_t mesh_index;
    uint32_t material_index;
    uint32_t transform_index; // can be root or bone-derived
    uint32_t skin_index;      // usually same as GameObject, or ~0u for rigid
    core::AABB local_AABB;
    uint32_t flags;
};
}; // namespace scene