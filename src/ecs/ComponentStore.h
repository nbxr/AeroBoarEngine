#pragma once

#include "ecs/Entity.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ecs {

// Dense SoA-style store: packed components + sparse entity → index.
template <typename T>
class ComponentStore {
  public:
    bool has(Entity e) const { return entity_to_index_.count(e) != 0; }

    T* try_get(Entity e) {
        auto it = entity_to_index_.find(e);
        if (it == entity_to_index_.end())
            return nullptr;
        return &data_[it->second];
    }

    const T* try_get(Entity e) const {
        auto it = entity_to_index_.find(e);
        if (it == entity_to_index_.end())
            return nullptr;
        return &data_[it->second];
    }

    T& get_or_emplace(Entity e, const T& value = T{}) {
        auto it = entity_to_index_.find(e);
        if (it != entity_to_index_.end())
            return data_[it->second];
        const uint32_t idx = static_cast<uint32_t>(data_.size());
        entity_to_index_[e] = idx;
        entities_.push_back(e);
        data_.push_back(value);
        return data_.back();
    }

    bool remove(Entity e) {
        auto it = entity_to_index_.find(e);
        if (it == entity_to_index_.end())
            return false;
        const uint32_t idx = it->second;
        const uint32_t last = static_cast<uint32_t>(data_.size() - 1);
        if (idx != last) {
            data_[idx] = std::move(data_[last]);
            entities_[idx] = entities_[last];
            entity_to_index_[entities_[idx]] = idx;
        }
        data_.pop_back();
        entities_.pop_back();
        entity_to_index_.erase(it);
        return true;
    }

    void clear() {
        data_.clear();
        entities_.clear();
        entity_to_index_.clear();
    }

    [[nodiscard]] uint32_t size() const {
        return static_cast<uint32_t>(data_.size());
    }
    [[nodiscard]] const std::vector<Entity>& entities() const { return entities_; }
    [[nodiscard]] std::vector<T>& data() { return data_; }
    [[nodiscard]] const std::vector<T>& data() const { return data_; }

  private:
    std::vector<T> data_;
    std::vector<Entity> entities_;
    std::unordered_map<Entity, uint32_t> entity_to_index_;
};

} // namespace ecs
