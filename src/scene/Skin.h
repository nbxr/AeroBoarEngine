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

    // Palette SSBO (GpuOnly) + static IBM/meta + compute pipeline.
    bool create_gpu_buffers(VkDevice device, VmaAllocator allocator);

    // CPU fallback (tests / if compute failed). Frame path uses record().
    void update_joint_matrices(uint32_t frame_index, const TransformManager& transforms);

    // Dispatch palette build. worlds must already be uploaded for this FIF slot.
    // Record outside any render pass, before skinned draws (including prepass).
    void record(VkCommandBuffer cmd, uint32_t frame_index,
                const gfx::AllocatedBuffer& worlds, uint32_t world_count);

    // Call after GpuCulling::build_scene (worlds buffer handle is stable).
    void bind_worlds(const gfx::AllocatedBuffer& worlds0,
                     const gfx::AllocatedBuffer& worlds1);

    [[nodiscard]] bool gpu_compute_ready() const {
        return gpu_ready_ && palette_pipeline_ != VK_NULL_HANDLE &&
               total_joints_ > 0;
    }

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
    bool create_compute(VkDevice device);
    void destroy_compute(VkDevice device, VmaAllocator allocator);
    void write_set(uint32_t frame, const gfx::AllocatedBuffer& worlds);

    std::vector<Skin> skins_{};
    std::vector<glm::mat4> cpu_palette_{};
    std::array<gfx::AllocatedBuffer, kMaxFrames> joint_buffers_{};
    gfx::AllocatedBuffer ibm_buffer_{};
    gfx::AllocatedBuffer meta_buffer_{};
    uint32_t total_joints_ = 0;
    bool gpu_ready_ = false;

    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline palette_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFrames> sets_{};
};

} // namespace scene
