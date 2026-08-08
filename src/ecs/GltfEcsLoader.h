#pragma once

#include "ecs/Entity.h"
#include <cstdint>
#include <vector>

namespace tinygltf {
class Model;
}

namespace scene {
class SceneManager;
}

namespace ecs {

class World;

// After TransformManager + GameObjects are built for a glTF:
//  - Dual-write: one Entity per mesh GameObject (TransformLink)
//  - Parse node extras.ECS_Components_v1 onto those entities (or create entity for non-mesh nodes)
//  - Bind scripts via ScriptRegistry
//  - If no PlayerTag: spawn default free-fly player
//  - resolve_active_player()
//
// Returns count of entities that received at least one ECS_Components_v1 entry.
uint32_t populate_world_from_gltf(World& world, const tinygltf::Model& model,
                                  scene::SceneManager& scene);

// Apply one node's ECS_Components_v1 array onto an existing entity.
// Returns number of component entries applied (unknown types skipped).
uint32_t apply_ecs_components_v1(World& world, Entity entity,
                                 const tinygltf::Model& model, int node_index);

} // namespace ecs
