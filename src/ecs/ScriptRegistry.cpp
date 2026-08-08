#include "ecs/ScriptRegistry.h"
#include "core/Log.h"

namespace ecs {

ScriptRegistry& ScriptRegistry::instance() {
    static ScriptRegistry reg;
    return reg;
}

void ScriptRegistry::add(const char* name, ScriptFactory factory) {
    if (!name || !factory)
        return;
    factories_[name] = factory;
}

std::unique_ptr<IScript> ScriptRegistry::create(const std::string& name) const {
    auto it = factories_.find(name);
    if (it == factories_.end()) {
        LOG_ERROR("[ECS] Unknown script name '" << name << "' (not REGISTER_SCRIPT'd)");
        return nullptr;
    }
    return it->second();
}

bool ScriptRegistry::has(const std::string& name) const {
    return factories_.count(name) != 0;
}

} // namespace ecs
