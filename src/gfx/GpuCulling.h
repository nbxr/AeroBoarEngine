#pragma once

#include "gfx/AllocatedBuffer.h"
#include "gfx/AllocatedImage.h"
#include "gfx/DrawBatch.h"
#include "gfx/MaterialManager.h"
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
// No model matrix — worlds live in a packed SSBO (one mat4 per transform).
// skin.w = transform_index; opaque vs blend is derived from flags (skin.z).
struct GpuCullItem {
    glm::vec4 aabb_min{0.0f};
    glm::vec4 aabb_max{0.0f};
    glm::uvec4 meta{0}; // x=material, y=batch, z=instance_base, w=capacity
    // x=joint_base, y=joint_count, z=material flags, w=transform_index
    glm::uvec4 skin{0};
};
static_assert(sizeof(GpuCullItem) == 64, "GpuCullItem size");

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
    uint32_t hzb_enabled = 0;       // 1 = Hi-Z occlusion test
    uint32_t hzb_mips = 0;
    glm::mat4 view_proj{1.0f};      // also hzb_view_proj (same-frame)
    glm::vec4 hzb_info{0.0f};       // xy = HZB mip0 size, z = depth bias
    // emit_filter: 0 = all, 1 = opaque depth writers only, 2 = transparent only
    uint32_t emit_filter = 0;
    // Offset into the combined out_instances[] (0 = opaque half, N = transparent half).
    // One SSBO for both passes so graphics binding 1 never flips mid-command-buffer
    // (UPDATE_AFTER_BIND would otherwise make both draws see the last write).
    uint32_t instance_base_offset = 0;
    uint32_t pad1 = 0;
    uint32_t pad2 = 0;
};

// Cull list filter for GpuCulling::record
enum class CullEmitFilter : uint32_t {
    All = 0,
    OpaqueDepth = 1,   // solid occluders (prepass / Hi-Z source, opaque shade)
    Transparent = 2,   // blend / transmission shade pass
};

// Pass selects which half of the combined instance SSBO + which indirect buffer.
enum class CullPass : uint32_t {
    Opaque = 0,
    Transparent = 1,
};
static constexpr uint32_t kCullPassCount = 2;

// std140: planes 96 + 4 uints 16 + mat4 64 + vec4 16 + 4 uints 16 = 208
static_assert(sizeof(GpuCullGlobals) == 208, "GpuCullGlobals std140 size");

// Fixed-region GPU frustum + Hi-Z occlusion cull + indirect command build.
class GpuCulling {
  public:
    bool initialize(VkDevice device, VmaAllocator allocator);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Build cull items / batch metas from mesh_draw_infos + scene.
    // Call after scene load. Allocates per-frame item + output buffers.
    // gpu_only_instances: true when WBOIT will GPU-emit (Adreno: unmapped).
    // false keeps instances host-mapped for the CPU-sort fallback.
    bool build_scene(VkDevice device, VmaAllocator allocator,
                     const std::vector<MeshDrawInfo>& mesh_draw_infos,
                     const scene::SceneManager& scene,
                     const MaterialManager* materials = nullptr,
                     bool gpu_only_instances = true);

    void clear_scene(VkDevice device, VmaAllocator allocator);

    // Upload the packed world-matrix table for this FIF slot (after fence wait).
    void update_models(uint32_t frame_index, const scene::SceneManager& scene);

    // Bind Hi-Z image for this frame slot (both pass descriptor sets). Call only
    // when the frame fence has been waited. Not UPDATE_AFTER_BIND — never mid-record.
    void bind_hzb(uint32_t frame_index, VkImageView hzb_view, VkSampler hzb_sampler);

    // Record: GPU-zero counts, frustum (+ optional same-frame HZB) cull, build indirect.
    // Must be called *outside* any render pass (barriers + dispatch).
    // view_proj: current camera. enable_hzb requires prior bind_hzb to the pyramid.
    // emit_filter: which materials pack into the draw list (see CullEmitFilter).
    // OpaqueDepth/All → CullPass::Opaque half; Transparent → Transparent half.
    void record(VkCommandBuffer cmd, uint32_t frame_index, const glm::mat4& view_proj,
                bool enable_hzb = false, uint32_t hzb_width = 0, uint32_t hzb_height = 0,
                uint32_t hzb_mips = 0, float hzb_depth_bias = 0.003f,
                CullEmitFilter emit_filter = CullEmitFilter::All);

    [[nodiscard]] bool is_ready() const { return ready_; }
    [[nodiscard]] uint32_t batch_count() const { return batch_count_; }
    [[nodiscard]] uint32_t item_count() const { return item_count_; }
    // Blend / transmission items (CPU transparent path). 0 → skip collect.
    [[nodiscard]] uint32_t transparent_item_count() const {
        return transparent_item_count_;
    }
    [[nodiscard]] bool has_transparent_half() const { return has_transparent_half_; }
    // Capacity of one pass half (firstInstance bases are relative to this).
    [[nodiscard]] uint32_t instance_slot_count() const { return instance_slot_count_; }

    // Combined instance SSBO for graphics binding 1 (opaque half + transparent half).
    // Bound at build_scene — do not re-point binding 1 mid-command-buffer.
    [[nodiscard]] AllocatedBuffer& worlds(uint32_t frame) { return worlds_[frame]; }
    [[nodiscard]] uint32_t world_count() const { return world_count_; }

    [[nodiscard]] AllocatedBuffer& out_instances(uint32_t frame) {
        return out_instances_[frame];
    }
    [[nodiscard]] AllocatedBuffer& indirect_cmds(uint32_t frame,
                                                 CullPass pass = CullPass::Opaque) {
        return indirect_cmds_[frame][static_cast<uint32_t>(pass)];
    }

    // After fence wait for this frame slot, previous counts are still valid until record().
    // Sums visible draws across both pass lists.
    [[nodiscard]] uint32_t read_visible_count(uint32_t frame_index) const;

  private:
    static constexpr uint32_t kMaxFrames = 2;

    bool create_pipelines(VkDevice device);
    bool create_descriptors(VkDevice device);

    static CullPass pass_for_filter(CullEmitFilter filter) {
        return (filter == CullEmitFilter::Transparent) ? CullPass::Transparent
                                                       : CullPass::Opaque;
    }

    VkDevice device_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline cull_pipeline_ = VK_NULL_HANDLE;
    VkPipeline build_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    // sets_[frame][pass] — different counts/indirect/globals; shared out_instances.
    std::array<std::array<VkDescriptorSet, kCullPassCount>, kMaxFrames> sets_{};
    // Dummy 1x1 for when HZB is disabled (descriptor must be valid).
    AllocatedImage dummy_hzb_{};
    VkSampler dummy_sampler_ = VK_NULL_HANDLE;
    bool dummy_layout_ready_ = false;

    // Per-frame item buffers so world uploads after fence wait are FIF-safe.
    // out_instances: GpuOnly (compute write → VS). Optional transparent half.
    std::array<AllocatedBuffer, kMaxFrames> cull_items_{};
    std::array<AllocatedBuffer, kMaxFrames> worlds_{};
    AllocatedBuffer batch_metas_{};
    std::array<std::array<AllocatedBuffer, kCullPassCount>, kMaxFrames> cull_globals_{};
    std::array<std::array<AllocatedBuffer, kCullPassCount>, kMaxFrames> batch_counts_{};
    std::array<AllocatedBuffer, kMaxFrames> out_instances_{};
    std::array<std::array<AllocatedBuffer, kCullPassCount>, kMaxFrames> indirect_cmds_{};

    // Parallel to cull items: TransformManager index per item (for update_models).
    std::vector<uint32_t> item_transform_indices_{};
    // CPU template (aabb/meta fixed; models refreshed via update_models).
    std::vector<GpuCullItem> cpu_items_{};

    uint32_t item_count_ = 0;
    uint32_t transparent_item_count_ = 0;
    uint32_t batch_count_ = 0;
    uint32_t instance_slot_count_ = 0; // one pass half size; transparent offset = this
    uint32_t world_count_ = 0;
    bool has_transparent_half_ = false;
    bool ready_ = false;
};

} // namespace gfx
