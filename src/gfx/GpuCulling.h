#pragma once

#include "gfx/AllocatedBuffer.h"
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
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
};
static_assert(sizeof(GpuCullGlobals) == 112, "GpuCullGlobals std140 size");

// Fixed-region GPU frustum cull + indirect command build.
class GpuCulling {
  public:
    bool initialize(VkDevice device, VmaAllocator allocator);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Build static cull items / batch metas from mesh_draw_infos + scene.
    // Call after scene load. Allocates per-frame output buffers.
    bool build_scene(VkDevice device, VmaAllocator allocator,
                     const std::vector<MeshDrawInfo>& mesh_draw_infos,
                     const scene::SceneManager& scene);

    void clear_scene(VkDevice device, VmaAllocator allocator);

    // Record: fill counts=0, cull dispatch, build indirect, barriers.
    // Writes into frame slot outputs; graphics binding 1 must use out_instances[frame].
    void record(VkCommandBuffer cmd, uint32_t frame_index, const glm::mat4& view_proj);

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

    // CPU-side bases for push.extra.x (static after build_scene)
    [[nodiscard]] const std::vector<uint32_t>& batch_bases() const { return batch_bases_; }

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

    AllocatedBuffer cull_items_{};
    AllocatedBuffer batch_metas_{};
    std::array<AllocatedBuffer, kMaxFrames> cull_globals_{};
    std::array<AllocatedBuffer, kMaxFrames> batch_counts_{};
    std::array<AllocatedBuffer, kMaxFrames> out_instances_{};
    std::array<AllocatedBuffer, kMaxFrames> indirect_cmds_{};

    std::vector<uint32_t> batch_bases_{};
    uint32_t item_count_ = 0;
    uint32_t batch_count_ = 0;
    bool ready_ = false;
};

} // namespace gfx
