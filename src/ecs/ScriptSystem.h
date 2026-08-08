#pragma once

#include "ecs/Entity.h"

namespace ecs {

class World;

// Call on_update for every entity with a live Script instance.
void script_system_update(World& world, float dt);

// Instantiate script from component name (registry); calls on_create.
// Returns false if name unknown or create failed.
bool script_system_bind(World& world, Entity entity);

// on_destroy + free instance for one entity's Script component.
void script_system_unbind(World& world, Entity entity);

} // namespace ecs

