#include "scene/TransformManager.h"
#include "core/Log.h"

namespace scene {

uint32_t TransformManager::allocate() {
    uint32_t index = kInvalid;
    if (!free_list_.empty()) {
        index = free_list_.back();
        free_list_.pop_back();
        world_matrices_[index] = glm::mat4(1.0f);
        parents_[index] = kInvalid;
        alive_[index] = 1;
        return index;
    }
    index = static_cast<uint32_t>(world_matrices_.size());
    world_matrices_.emplace_back(1.0f);
    parents_.push_back(kInvalid);
    alive_.push_back(1);
    return index;
}

void TransformManager::free(uint32_t index) {
    if (!is_alive(index))
        return;
    alive_[index] = 0;
    world_matrices_[index] = glm::mat4(1.0f);
    parents_[index] = kInvalid;
    free_list_.push_back(index);
}

void TransformManager::set_world_matrix(uint32_t index, const glm::mat4& world) {
    if (!is_alive(index)) {
        LOG_ERROR("[TransformManager] set_world_matrix on invalid index " << index);
        return;
    }
    world_matrices_[index] = world;
}

const glm::mat4& TransformManager::get_world_matrix(uint32_t index) const {
    static const glm::mat4 kIdentity(1.0f);
    if (!is_alive(index))
        return kIdentity;
    return world_matrices_[index];
}

void TransformManager::set_parent(uint32_t index, uint32_t parent_index) {
    if (!is_alive(index))
        return;
    parents_[index] = parent_index;
}

uint32_t TransformManager::get_parent(uint32_t index) const {
    if (index >= parents_.size())
        return kInvalid;
    return parents_[index];
}

bool TransformManager::is_alive(uint32_t index) const {
    return index < alive_.size() && alive_[index] != 0;
}

void TransformManager::clear() {
    world_matrices_.clear();
    parents_.clear();
    alive_.clear();
    free_list_.clear();
}

} // namespace scene
