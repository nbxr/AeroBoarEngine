#pragma once

#include "ecs/Entity.h"
#include "ecs/Event.h"
#include <memory>

namespace ecs {

class World;

struct IScript {
    virtual ~IScript() = default;
    virtual void on_create(World& world, Entity entity) {}
    virtual void on_update(World& world, Entity entity, float dt) {}
    virtual void on_event(World& world, Entity entity, const Event& event) {}
    virtual void on_destroy(World& world, Entity entity) {}
};

using ScriptFactory = std::unique_ptr<IScript> (*)();

} // namespace ecs

