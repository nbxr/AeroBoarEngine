#include "scene/TransformManager.h"
#include "core/Log.h"

#include <algorithm>

namespace scene {

uint32_t TransformManager::allocate() {
    uint32_t index = kInvalid;
    if (!free_list_.empty()) {
        index = free_list_.back();
        free_list_.pop_back();
        local_matrices_[index] = glm::mat4(1.0f);
        world_matrices_[index] = glm::mat4(1.0f);
        parents_[index] = kInvalid;
        children_[index].clear();
        alive_[index] = 1;
        return index;
    }
    index = static_cast<uint32_t>(world_matrices_.size());
    local_matrices_.emplace_back(1.0f);
    world_matrices_.emplace_back(1.0f);
    parents_.push_back(kInvalid);
    children_.emplace_back();
    alive_.push_back(1);
    return index;
}

void TransformManager::detach_from_parent(uint32_t index) {
    const uint32_t p = parents_[index];
    if (p == kInvalid || p >= children_.size())
        return;
    auto& kids = children_[p];
    kids.erase(std::remove(kids.begin(), kids.end(), index), kids.end());
    parents_[index] = kInvalid;
}

void TransformManager::free(uint32_t index) {
    if (!is_alive(index))
        return;

    // Orphan children to roots (keep their current world as local approx).
    for (uint32_t child : children_[index]) {
        if (child < parents_.size())
            parents_[child] = kInvalid;
    }
    children_[index].clear();
    detach_from_parent(index);

    alive_[index] = 0;
    local_matrices_[index] = glm::mat4(1.0f);
    world_matrices_[index] = glm::mat4(1.0f);
    free_list_.push_back(index);
}

void TransformManager::set_local_matrix(uint32_t index, const glm::mat4& local) {
    if (!is_alive(index)) {
        LOG_ERROR("[TransformManager] set_local_matrix on invalid index " << index);
        return;
    }
    local_matrices_[index] = local;
}

const glm::mat4& TransformManager::get_local_matrix(uint32_t index) const {
    static const glm::mat4 kIdentity(1.0f);
    if (!is_alive(index))
        return kIdentity;
    return local_matrices_[index];
}

void TransformManager::set_world_matrix(uint32_t index, const glm::mat4& world) {
    if (!is_alive(index)) {
        LOG_ERROR("[TransformManager] set_world_matrix on invalid index " << index);
        return;
    }
    world_matrices_[index] = world;
    // Keep local in sync if root; if parented, local becomes stale until set_local.
    if (parents_[index] == kInvalid)
        local_matrices_[index] = world;
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
    if (parent_index != kInvalid && !is_alive(parent_index)) {
        LOG_ERROR("[TransformManager] set_parent: invalid parent " << parent_index);
        return;
    }
    if (parent_index == index) {
        LOG_ERROR("[TransformManager] set_parent: cannot parent to self");
        return;
    }

    detach_from_parent(index);
    parents_[index] = parent_index;
    if (parent_index != kInvalid)
        children_[parent_index].push_back(index);
}

uint32_t TransformManager::get_parent(uint32_t index) const {
    if (index >= parents_.size())
        return kInvalid;
    return parents_[index];
}

void TransformManager::propagate_recursive(uint32_t index) {
    if (!is_alive(index))
        return;

    const uint32_t p = parents_[index];
    if (p == kInvalid || !is_alive(p)) {
        world_matrices_[index] = local_matrices_[index];
    } else {
        world_matrices_[index] = world_matrices_[p] * local_matrices_[index];
    }

    for (uint32_t child : children_[index])
        propagate_recursive(child);
}

void TransformManager::propagate() {
    // Roots only — recursion covers descendants.
    for (uint32_t i = 0; i < world_matrices_.size(); ++i) {
        if (!is_alive(i))
            continue;
        if (parents_[i] == kInvalid || !is_alive(parents_[i]))
            propagate_recursive(i);
    }
}

bool TransformManager::is_alive(uint32_t index) const {
    return index < alive_.size() && alive_[index] != 0;
}

void TransformManager::clear() {
    local_matrices_.clear();
    world_matrices_.clear();
    parents_.clear();
    children_.clear();
    alive_.clear();
    free_list_.clear();
}

} // namespace scene
