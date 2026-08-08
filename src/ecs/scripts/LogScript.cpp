#include "ecs/IScript.h"
#include "ecs/ScriptRegistry.h"
#include "ecs/World.h"
#include "core/Log.h"

namespace {

// Example registered script: log on create (and optionally heartbeat).
// Author as: { "type": "script", "name": "log" }
class LogScript : public ecs::IScript {
  public:
    void on_create(ecs::World& world, ecs::Entity entity) override {
        const char* label = "?";
        if (const ecs::Name* n = world.names.try_get(entity))
            label = n->value.c_str();
        LOG_INFO("[Script:log] on_create entity=" << entity << " name='" << label
                 << "'");
    }

    void on_update(ecs::World& /*world*/, ecs::Entity /*entity*/,
                   float /*dt*/) override {
        // Keep quiet after create — use for debugging only.
    }

    void on_destroy(ecs::World& /*world*/, ecs::Entity entity) override {
        LOG_INFO("[Script:log] on_destroy entity=" << entity);
    }
};

REGISTER_SCRIPT("log", LogScript);

} // namespace
