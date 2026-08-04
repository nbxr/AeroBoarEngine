#pragma once

#include "gfx/AllocatedBuffer.h"
#include "scene/TransformManager.h"
#include <array>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace tinygltf {
class Model;
}

namespace scene {

constexpr uint32_t kInvalidSkin = ~0u;
constexpr uint32_t kMaxJointsTotal = 256; // global palette capacity

// One glTF skin: joint transform indices + inverse bind matrices.
struct Skin {
    std::vector<uint32_t> joint_transform_indices; // TransformManager slots
    std::vector<glm::mat4> inverse_bind_matrices;
    uint32_t palette_offset = 0; // base index into global joint matrix buffer
    // Node that owns the skinned mesh (for inv(meshWorld) * jointWorld * IBM).
    // kInvalid → treat mesh world as identity.
    uint32_t mesh_transform_index = TransformManager::kInvalid;
};

// Loads skins, packs joint palettes (jointWorld * IBM), uploads to GPU SSBO.
class SkinSystem {
  public:
    static constexpr uint32_t kMaxFrames = 2;

    void clear();
    void destroy(VkDevice device, VmaAllocator allocator);

    // After gltf_node_to_transform is filled. Returns number of skins.
    uint32_t load_from_gltf(const tinygltf::Model& model,
                            const std::vector<uint32_t>& gltf_node_to_transform);

    // Record mesh node transform for inv(meshWorld) skinning (first writer wins).
    void set_mesh_transform(uint32_t skin_index, uint32_t mesh_transform_index);

    // Allocate host-visible per-frame joint buffers (call after load if skins exist).
    bool create_gpu_buffers(VkDevice device, VmaAllocator allocator);

    // Palette entry = inv(meshWorld) * jointWorld * IBM (glTF). VS applies mesh model.
    void update_joint_matrices(uint32_t frame_index, const TransformManager& transforms);

    [[nodiscard]] uint32_t skin_count() const {
        return static_cast<uint32_t>(skins_.size());
    }
    [[nodiscard]] const Skin& skin(uint32_t i) const { return skins_[i]; }
    [[nodiscard]] bool has_skins() const { return !skins_.empty(); }
    [[nodiscard]] uint32_t total_joints() const { return total_joints_; }

    [[nodiscard]] gfx::AllocatedBuffer& joint_buffer(uint32_t frame) {
        return joint_buffers_[frame];
    }
    [[nodiscard]] VkDeviceSize joint_buffer_size() const {
        return sizeof(glm::mat4) * std::max(1u, total_joints_);
    }

  private:
    std::vector<Skin> skins_{};
    std::vector<glm::mat4> cpu_palette_{};
    std::array<gfx::AllocatedBuffer, kMaxFrames> joint_buffers_{};
    uint32_t total_joints_ = 0;
    bool gpu_ready_ = false;
};

} // namespace scene
