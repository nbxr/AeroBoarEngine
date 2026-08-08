#include "ecs/ScriptSystem.h"
#include "ecs/ScriptRegistry.h"
#include "ecs/World.h"
#include "core/Log.h"

namespace ecs {

bool script_system_bind(World& world, Entity entity) {
    Script* sc = world.scripts.try_get(entity);
    if (!sc || sc->name.empty())
        return false;
    if (sc->instance_handle != ScriptInstanceStore::kInvalid)
        return true; // already bound

    auto inst = ScriptRegistry::instance().create(sc->name);
    if (!inst)
        return false;

    IScript* raw = inst.get();
    sc->instance_handle = world.script_instances.add(std::move(inst));
    if (sc->instance_handle == ScriptInstanceStore::kInvalid)
        return false;

    raw->on_create(world, entity);
    return true;
}

void script_system_unbind(World& world, Entity entity) {
    Script* sc = world.scripts.try_get(entity);
    if (!sc || sc->instance_handle == ScriptInstanceStore::kInvalid)
        return;
    if (IScript* s = world.script_instances.get(sc->instance_handle))
        s->on_destroy(world, entity);
    world.script_instances.destroy(sc->instance_handle);
    sc->instance_handle = ScriptInstanceStore::kInvalid;
}

void script_system_update(World& world, float dt) {
    const auto& ents = world.scripts.entities();
    auto& data = world.scripts.data();
    for (size_t i = 0; i < ents.size(); ++i) {
        const Entity e = ents[i];
        Script& sc = data[i];
        if (sc.instance_handle == ScriptInstanceStore::kInvalid)
            continue;
        if (IScript* s = world.script_instances.get(sc.instance_handle))
            s->on_update(world, e, dt);
    }
}

} // namespace ecs
