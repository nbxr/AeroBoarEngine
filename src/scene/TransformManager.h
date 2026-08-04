#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace scene {

// Local TRS (glTF-style). Local matrix = T * R * S.
struct LocalTrs {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // w, x, y, z
    glm::vec3 scale{1.0f};
};

// Dense transform storage with hierarchy + dirty-flag propagate.
// - local TRS + local_matrices_ (kept in sync for animation component updates)
// - parents_ / children_: hierarchy links
// - world_matrices_: composed world = parent_world * local (via propagate())
//
// Call set_local_* / set_parent to mutate; call propagate() (or
// propagate_if_dirty()) before reading worlds for cull / draw / lights.
class TransformManager {
  public:
    static constexpr uint32_t kInvalid = ~0u;

    // Allocate a transform slot; local/world default to identity, parent = root.
    uint32_t allocate();

    // Free a slot for reuse (does not compact the dense array). Detaches children.
    void free(uint32_t index);

    // Full local TRS (preferred for glTF load + animation).
    void set_local_trs(uint32_t index, const LocalTrs& trs);
    void set_local_trs(uint32_t index, const glm::vec3& t, const glm::quat& r,
                       const glm::vec3& s);
    [[nodiscard]] const LocalTrs& get_local_trs(uint32_t index) const;

    // Partial TRS updates (rebuild local matrix from stored components).
    void set_local_translation(uint32_t index, const glm::vec3& t);
    void set_local_rotation(uint32_t index, const glm::quat& r);
    void set_local_scale(uint32_t index, const glm::vec3& s);

    // Marks the node dirty. Also best-effort decomposes into TRS for later anim.
    void set_local_matrix(uint32_t index, const glm::mat4& local);
    [[nodiscard]] const glm::mat4& get_local_matrix(uint32_t index) const;

    // Direct world write (skips hierarchy). Prefer set_local + propagate for nodes.
    // Marks dirty so dependents still refresh if mixed with hierarchy.
    void set_world_matrix(uint32_t index, const glm::mat4& world);
    [[nodiscard]] const glm::mat4& get_world_matrix(uint32_t index) const;

    // parent_index = kInvalid for scene roots. Updates children lists; marks dirty.
    void set_parent(uint32_t index, uint32_t parent_index);
    [[nodiscard]] uint32_t get_parent(uint32_t index) const;

    // Recompute world matrices for dirty nodes (and their descendants).
    // Returns true if any world matrix was updated.
    bool propagate();

    // No-op if nothing is dirty; otherwise same as propagate().
    bool propagate_if_dirty() {
        if (!any_dirty_)
            return false;
        return propagate();
    }

    [[nodiscard]] bool any_dirty() const { return any_dirty_; }
    [[nodiscard]] bool is_dirty(uint32_t index) const;

    // Force full recompute next propagate() (e.g. after bulk load edits).
    void mark_all_dirty();

    [[nodiscard]] uint32_t count() const {
        return static_cast<uint32_t>(world_matrices_.size());
    }
    [[nodiscard]] bool is_alive(uint32_t index) const;

    void clear();

    [[nodiscard]] const std::vector<glm::mat4>& world_matrices() const {
        return world_matrices_;
    }
    [[nodiscard]] const std::vector<glm::mat4>& local_matrices() const {
        return local_matrices_;
    }

  private:
    void detach_from_parent(uint32_t index);
    void mark_dirty(uint32_t index);
    void propagate_recursive(uint32_t index);
    void rebuild_local_matrix(uint32_t index);
    static glm::mat4 compose_trs(const LocalTrs& trs);

    std::vector<LocalTrs> local_trs_{};
    std::vector<glm::mat4> local_matrices_{};
    std::vector<glm::mat4> world_matrices_{};
    std::vector<uint32_t> parents_{}; // kInvalid = root
    std::vector<std::vector<uint32_t>> children_{};
    std::vector<uint8_t> alive_{};
    std::vector<uint8_t> dirty_{}; // 1 = needs world recompute
    std::vector<uint32_t> free_list_{};
    bool any_dirty_ = false;
};

} // namespace scene
