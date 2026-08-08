#pragma once

#include "ecs/IScript.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace ecs {

// Polymorphic script instances; handles stored on Script component.
class ScriptInstanceStore {
  public:
    uint32_t add(std::unique_ptr<IScript> script);
    IScript* get(uint32_t handle);
    const IScript* get(uint32_t handle) const;
    void destroy(uint32_t handle);
    void clear();

    static constexpr uint32_t kInvalid = ~0u;

  private:
    std::vector<std::unique_ptr<IScript>> slots_;
    std::vector<uint32_t> free_list_;
};

} // namespace ecs
