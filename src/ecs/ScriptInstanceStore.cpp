#include "ecs/ScriptInstanceStore.h"

namespace ecs {

uint32_t ScriptInstanceStore::add(std::unique_ptr<IScript> script) {
    if (!script)
        return kInvalid;
    if (!free_list_.empty()) {
        const uint32_t h = free_list_.back();
        free_list_.pop_back();
        slots_[h] = std::move(script);
        return h;
    }
    const uint32_t h = static_cast<uint32_t>(slots_.size());
    slots_.push_back(std::move(script));
    return h;
}

IScript* ScriptInstanceStore::get(uint32_t handle) {
    if (handle >= slots_.size())
        return nullptr;
    return slots_[handle].get();
}

const IScript* ScriptInstanceStore::get(uint32_t handle) const {
    if (handle >= slots_.size())
        return nullptr;
    return slots_[handle].get();
}

void ScriptInstanceStore::destroy(uint32_t handle) {
    if (handle >= slots_.size() || !slots_[handle])
        return;
    slots_[handle].reset();
    free_list_.push_back(handle);
}

void ScriptInstanceStore::clear() {
    slots_.clear();
    free_list_.clear();
}

} // namespace ecs
