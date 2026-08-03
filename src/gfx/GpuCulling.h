#pragma once

#include "gfx/AllocatedBuffer.h"
#include "gfx/AllocatedImage.h"
#include "gfx/DrawBatch.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace scene {
class SceneManager;
}

namespace gfx {

// GPU layouts (std430) matching cull_frustum.comp / build_indirect.comp
struct GpuCullItem {
    glm::mat4 model{1.0f};
    glm::vec4 aabb_min{0.0f};
    glm::vec4 aabb_max{0.0f};
    glm::uvec4 meta{0}; // x=material, y=batch, z=instance_base, w=capacity
};
static_assert(sizeof(GpuCullItem) == 112, "GpuCullItem size");

struct GpuBatchMeta {
    uint32_t base = 0;
    uint32_t capacity = 0;
    uint32_t index_count = 0;
    uint32_t first_index = 0;
    int32_t  vertex_offset = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
};
static_assert(sizeof(GpuBatchMeta) == 32, "GpuBatchMeta size");

struct GpuCullGlobals {
    glm::vec4 planes[6]{};
    uint32_t item_count = 0;
    uint32_t batch_count = 0;
    uint32_t hzb_enabled = 0;
    uint32_t hzb_mips = 0;
    glm::mat4 view_proj{1.0f};
    glm::vec4 hzb_info{0.0f}; // xy = HZB mip0 size, z = depth bias
};
// std140: planes 96 + 4 uints 16 + mat4 64 + vec4 16 = 192
static_assert(sizeof(GpuCullGlobals) == 192, "GpuCullGlobals std140 size");

// Fixed-region GPU frustum + Hi-Z occlusion cull + indirect command build.
class GpuCulling {
  public:
    bool initialize(VkDevice device, VmaAllocator allocator);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Build cull items / batch metas from mesh_draw_infos + scene.
    // Call after scene load. Allocates per-frame item + output buffers.
    bool build_scene(VkDevice device, VmaAllocator allocator,
                     const std::vector<MeshDrawInfo>& mesh_draw_infos,
                     const scene::SceneManager& scene);

    void clear_scene(VkDevice device, VmaAllocator allocator);

    // Rewrite model matrices for one frame slot from current TransformManager worlds.
    // Call only after that frame's fence has been waited (buffer not in use on GPU).
    void update_models(uint32_t frame_index, const scene::SceneManager& scene);

    // Bind Hi-Z image for this frame slot. Call only when the frame fence has
    // been waited (set not in use). Not UPDATE_AFTER_BIND — never call mid-record.
    void bind_hzb(uint32_t frame_index, VkImageView hzb_view, VkSampler hzb_sampler);

    // Record: zero counts, frustum (+ optional same-frame HZB) cull, build indirect.
    // view_proj: current camera. enable_hzb requires prior bind_hzb to the pyramid.
    void record(VkCommandBuffer cmd, uint32_t frame_index, const glm::mat4& view_proj,
                bool enable_hzb = false, uint32_t hzb_width = 0, uint32_t hzb_height = 0,
                uint32_t hzb_mips = 0, float hzb_depth_bias = 0.003f);

    [[nodiscard]] bool is_ready() const { return ready_; }
    [[nodiscard]] uint32_t batch_count() const { return batch_count_; }
    [[nodiscard]] uint32_t item_count() const { return item_count_; }

    // Per-frame instance SSBO for graphics set binding 1
    [[nodiscard]] AllocatedBuffer& out_instances(uint32_t frame) {
        return out_instances_[frame];
    }
    [[nodiscard]] AllocatedBuffer& indirect_cmds(uint32_t frame) {
        return indirect_cmds_[frame];
    }

    // After fence wait for this frame slot, previous counts are still valid until record().
    [[nodiscard]] uint32_t read_visible_count(uint32_t frame_index) const;

  private:
    static constexpr uint32_t kMaxFrames = 2;

    bool create_pipelines(VkDevice device);
    bool create_descriptors(VkDevice device);

    VkDevice device_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline cull_pipeline_ = VK_NULL_HANDLE;
    VkPipeline build_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kMaxFrames> sets_{};
    // Dummy 1x1 for when HZB is disabled (descriptor must be valid).
    AllocatedImage dummy_hzb_{};
    VkSampler dummy_sampler_ = VK_NULL_HANDLE;
    bool dummy_layout_ready_ = false;

    // Per-frame item buffers so model updates after fence wait are FIF-safe.
    std::array<AllocatedBuffer, kMaxFrames> cull_items_{};
    AllocatedBuffer batch_metas_{};
    std::array<AllocatedBuffer, kMaxFrames> cull_globals_{};
    std::array<AllocatedBuffer, kMaxFrames> batch_counts_{};
    std::array<AllocatedBuffer, kMaxFrames> out_instances_{};
    std::array<AllocatedBuffer, kMaxFrames> indirect_cmds_{};

    // Parallel to cull items: TransformManager index per item (for update_models).
    std::vector<uint32_t> item_transform_indices_{};
    // CPU template (aabb/meta fixed; models refreshed via update_models).
    std::vector<GpuCullItem> cpu_items_{};

    uint32_t item_count_ = 0;
    uint32_t batch_count_ = 0;
    bool ready_ = false;
};

} // namespace gfx
