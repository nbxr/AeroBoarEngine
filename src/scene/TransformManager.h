#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace scene {

// Dense, cache-friendly transform storage (foundation for hierarchy + GPU culling).
// Currently stores final world matrices (filled at load). Future: local TRS SOA +
// parent indices with a propagate() pass (CPU or compute).
class TransformManager {
  public:
    static constexpr uint32_t kInvalid = ~0u;

    // Allocate a transform slot; returns index. Matrix defaults to identity.
    uint32_t allocate();

    // Free a slot for reuse (does not compact the dense array).
    void free(uint32_t index);

    void set_world_matrix(uint32_t index, const glm::mat4& world);
    [[nodiscard]] const glm::mat4& get_world_matrix(uint32_t index) const;

    void set_parent(uint32_t index, uint32_t parent_index);
    [[nodiscard]] uint32_t get_parent(uint32_t index) const;

    [[nodiscard]] uint32_t count() const {
        return static_cast<uint32_t>(world_matrices_.size());
    }
    [[nodiscard]] bool is_alive(uint32_t index) const;

    void clear();

    // Contiguous world matrices for GPU upload (includes freed slots as identity).
    [[nodiscard]] const std::vector<glm::mat4>& world_matrices() const {
        return world_matrices_;
    }

  private:
    std::vector<glm::mat4> world_matrices_{};
    std::vector<uint32_t> parents_{}; // kInvalid = root
    std::vector<uint8_t> alive_{};
    std::vector<uint32_t> free_list_{};
};

} // namespace scene
