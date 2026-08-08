#pragma once

#include "ecs/IScript.h"
#include <string>
#include <unordered_map>

namespace ecs {

// Process-lifetime name → factory map (Meyers singleton; safe with static registrars).
class ScriptRegistry {
  public:
    static ScriptRegistry& instance();

    void add(const char* name, ScriptFactory factory);
    [[nodiscard]] std::unique_ptr<IScript> create(const std::string& name) const;
    [[nodiscard]] bool has(const std::string& name) const;

  private:
    ScriptRegistry() = default;
    std::unordered_map<std::string, ScriptFactory> factories_;
};

struct ScriptRegistrar {
    ScriptRegistrar(const char* name, ScriptFactory factory) {
        ScriptRegistry::instance().add(name, factory);
    }
};

// Place in each script .cpp (must be linked into the executable):
//   REGISTER_SCRIPT("my_name", MyScriptClass);
#define REGISTER_SCRIPT(NameStr, ClassType)                                    \
    static ::ecs::ScriptRegistrar reg_##ClassType(                             \
        NameStr, []() -> std::unique_ptr<::ecs::IScript> {                     \
            return std::make_unique<ClassType>();                              \
        })

} // namespace ecs
