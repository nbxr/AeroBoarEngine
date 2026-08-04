#include "scene/TransformManager.h"
#include "core/Log.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace scene {
namespace {

LocalTrs identity_trs() {
    return {};
}

// Best-effort TRS extract for set_local_matrix (uniform scale / pure rotation).
LocalTrs decompose_matrix(const glm::mat4& m) {
    LocalTrs trs{};
    trs.translation = glm::vec3(m[3]);
    glm::vec3 col0(m[0]);
    glm::vec3 col1(m[1]);
    glm::vec3 col2(m[2]);
    trs.scale = glm::vec3(glm::length(col0), glm::length(col1), glm::length(col2));
    if (trs.scale.x > 1e-8f)
        col0 /= trs.scale.x;
    if (trs.scale.y > 1e-8f)
        col1 /= trs.scale.y;
    if (trs.scale.z > 1e-8f)
        col2 /= trs.scale.z;
    glm::mat3 rot(col0, col1, col2);
    trs.rotation = glm::normalize(glm::quat_cast(rot));
    return trs;
}

} // namespace

glm::mat4 TransformManager::compose_trs(const LocalTrs& trs) {
    return glm::translate(glm::mat4(1.0f), trs.translation) *
           glm::mat4_cast(glm::normalize(trs.rotation)) *
           glm::scale(glm::mat4(1.0f), trs.scale);
}

void TransformManager::rebuild_local_matrix(uint32_t index) {
    local_matrices_[index] = compose_trs(local_trs_[index]);
}

uint32_t TransformManager::allocate() {
    uint32_t index = kInvalid;
    if (!free_list_.empty()) {
        index = free_list_.back();
        free_list_.pop_back();
        local_trs_[index] = identity_trs();
        local_matrices_[index] = glm::mat4(1.0f);
        world_matrices_[index] = glm::mat4(1.0f);
        parents_[index] = kInvalid;
        children_[index].clear();
        alive_[index] = 1;
        dirty_[index] = 1;
        any_dirty_ = true;
        return index;
    }
    index = static_cast<uint32_t>(world_matrices_.size());
    local_trs_.push_back(identity_trs());
    local_matrices_.emplace_back(1.0f);
    world_matrices_.emplace_back(1.0f);
    parents_.push_back(kInvalid);
    children_.emplace_back();
    alive_.push_back(1);
    dirty_.push_back(1);
    any_dirty_ = true;
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

    for (uint32_t child : children_[index]) {
        if (child < parents_.size()) {
            parents_[child] = kInvalid;
            mark_dirty(child);
        }
    }
    children_[index].clear();
    detach_from_parent(index);

    alive_[index] = 0;
    dirty_[index] = 0;
    local_trs_[index] = identity_trs();
    local_matrices_[index] = glm::mat4(1.0f);
    world_matrices_[index] = glm::mat4(1.0f);
    free_list_.push_back(index);
}

void TransformManager::mark_dirty(uint32_t index) {
    if (!is_alive(index))
        return;
    dirty_[index] = 1;
    any_dirty_ = true;
}

void TransformManager::set_local_trs(uint32_t index, const LocalTrs& trs) {
    if (!is_alive(index)) {
        LOG_ERROR("[TransformManager] set_local_trs on invalid index " << index);
        return;
    }
    local_trs_[index] = trs;
    local_trs_[index].rotation = glm::normalize(local_trs_[index].rotation);
    rebuild_local_matrix(index);
    mark_dirty(index);
}

void TransformManager::set_local_trs(uint32_t index, const glm::vec3& t,
                                     const glm::quat& r, const glm::vec3& s) {
    LocalTrs trs{};
    trs.translation = t;
    trs.rotation = r;
    trs.scale = s;
    set_local_trs(index, trs);
}

const LocalTrs& TransformManager::get_local_trs(uint32_t index) const {
    static const LocalTrs kIdent{};
    if (!is_alive(index))
        return kIdent;
    return local_trs_[index];
}

void TransformManager::set_local_translation(uint32_t index, const glm::vec3& t) {
    if (!is_alive(index))
        return;
    local_trs_[index].translation = t;
    rebuild_local_matrix(index);
    mark_dirty(index);
}

void TransformManager::set_local_rotation(uint32_t index, const glm::quat& r) {
    if (!is_alive(index))
        return;
    local_trs_[index].rotation = glm::normalize(r);
    rebuild_local_matrix(index);
    mark_dirty(index);
}

void TransformManager::set_local_scale(uint32_t index, const glm::vec3& s) {
    if (!is_alive(index))
        return;
    local_trs_[index].scale = s;
    rebuild_local_matrix(index);
    mark_dirty(index);
}

void TransformManager::set_local_matrix(uint32_t index, const glm::mat4& local) {
    if (!is_alive(index)) {
        LOG_ERROR("[TransformManager] set_local_matrix on invalid index " << index);
        return;
    }
    local_matrices_[index] = local;
    local_trs_[index] = decompose_matrix(local);
    mark_dirty(index);
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
    if (parents_[index] == kInvalid) {
        local_matrices_[index] = world;
        local_trs_[index] = decompose_matrix(world);
    }
    mark_dirty(index);
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
    mark_dirty(index);
}

uint32_t TransformManager::get_parent(uint32_t index) const {
    if (index >= parents_.size())
        return kInvalid;
    return parents_[index];
}

bool TransformManager::is_dirty(uint32_t index) const {
    return index < dirty_.size() && dirty_[index] != 0;
}

void TransformManager::mark_all_dirty() {
    for (uint32_t i = 0; i < dirty_.size(); ++i) {
        if (alive_[i])
            dirty_[i] = 1;
    }
    any_dirty_ = !dirty_.empty();
}

void TransformManager::propagate_recursive(uint32_t index) {
    if (!is_alive(index))
        return;

    if (dirty_[index]) {
        const uint32_t p = parents_[index];
        if (p == kInvalid || !is_alive(p)) {
            world_matrices_[index] = local_matrices_[index];
        } else {
            world_matrices_[index] = world_matrices_[p] * local_matrices_[index];
        }
        dirty_[index] = 0;
        for (uint32_t child : children_[index]) {
            if (is_alive(child))
                dirty_[child] = 1;
        }
    }

    for (uint32_t child : children_[index])
        propagate_recursive(child);
}

bool TransformManager::propagate() {
    if (!any_dirty_)
        return false;

    for (uint32_t i = 0; i < world_matrices_.size(); ++i) {
        if (!is_alive(i))
            continue;
        if (parents_[i] == kInvalid || !is_alive(parents_[i]))
            propagate_recursive(i);
    }

    any_dirty_ = false;
    for (uint32_t i = 0; i < dirty_.size(); ++i) {
        if (alive_[i] && dirty_[i]) {
            any_dirty_ = true;
            break;
        }
    }
    return true;
}

bool TransformManager::is_alive(uint32_t index) const {
    return index < alive_.size() && alive_[index] != 0;
}

void TransformManager::clear() {
    local_trs_.clear();
    local_matrices_.clear();
    world_matrices_.clear();
    parents_.clear();
    children_.clear();
    alive_.clear();
    dirty_.clear();
    free_list_.clear();
    any_dirty_ = false;
}

} // namespace scene
