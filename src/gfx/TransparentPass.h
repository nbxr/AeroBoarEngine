#pragma once

#include "gfx/AllocatedImage.h"
#include "gfx/DrawBatch.h"
#include "core/AABB.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace core {
struct Frustum;
}

namespace scene {
class SceneManager;
}

namespace gfx {

class MaterialManager;
class MeshManager;
class GpuCulling;
struct Renderer;

// CPU back-to-front transparent list + weighted blended OIT (McGuire).
// Transparents still *test* the opaque 1x depth; they do not write it.
// Composite blends coverage onto the swapchain (no second scene depth).
class TransparentPass {
  public:
    static constexpr uint32_t kMaxFrames = 2;

    struct DrawItem {
        float sort_key = 0.0f; // larger = farther (draw first)
        DrawInstanceGPU instance{};
        uint32_t index_count = 0;
        uint32_t index_offset = 0;
        int32_t vertex_offset = 0;
    };

    bool create(VkDevice device, VmaAllocator allocator, Renderer& renderer);
    void destroy(VkDevice device, VmaAllocator allocator);
    bool resize(VkDevice device, VmaAllocator allocator, Renderer& renderer);
    // Drop gather/composite FBs that reference main-pass depth / swap views.
    // Call before destroying those images (swapchain recreate).
    void release_swapchain_views(VkDevice device);

    [[nodiscard]] bool is_ready() const { return ready_; }
    [[nodiscard]] bool wboit_ready() const { return wboit_ready_; }
    [[nodiscard]] uint32_t count() const {
        return static_cast<uint32_t>(items_.size());
    }
    void clear_items() { items_.clear(); }

    void collect_and_sort(const scene::SceneManager& scene,
                          const MaterialManager& materials,
                          const MeshManager& meshes,
                          const core::Frustum& frustum,
                          const glm::vec3& camera_pos);

    // Write sorted instances into the transparent half of the combined SSBO.
    void upload_instances(GpuCulling& culling, uint32_t frame_index);

    void record_sorted_draws(VkCommandBuffer cmd, Renderer& renderer,
                             VkPipeline pipeline, const glm::mat4& view_proj,
                             uint32_t instance_base) const;

    // gpu_emit: draw the GPU-culled transparent half (WBOIT default).
    // Otherwise uses the CPU-sorted items_ list.
    void record_wboit(VkCommandBuffer cmd, Renderer& renderer,
                      uint32_t frame_index, uint32_t image_index,
                      const glm::mat4& view_proj, bool gpu_emit = false);

  private:
    bool create_images(VkDevice device, VmaAllocator allocator, VkExtent2D extent);
    void destroy_images(VkDevice device, VmaAllocator allocator);
    void destroy_framebuffers(VkDevice device);
    bool create_render_passes(VkDevice device, VkFormat swap_format,
                              VkFormat depth_format);
    bool create_pipelines(VkDevice device, VkPipelineLayout pbr_layout);
    bool create_composite_descriptors(VkDevice device);
    bool create_framebuffers(VkDevice device, Renderer& renderer);

    static bool create_color_image(VkDevice device, VmaAllocator allocator,
                                   VkExtent2D extent, VkFormat format,
                                   AllocatedImage& out);

    VkDevice device_ = VK_NULL_HANDLE;
    bool ready_ = false;
    bool wboit_ready_ = false;

    std::vector<DrawItem> items_;

    VkRenderPass gather_rp_ = VK_NULL_HANDLE;
    VkRenderPass composite_rp_ = VK_NULL_HANDLE;
    VkPipeline gather_pipeline_ = VK_NULL_HANDLE;
    VkPipeline composite_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout composite_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout composite_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool composite_pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    std::array<AllocatedImage, kMaxFrames> accum_{};
    std::array<AllocatedImage, kMaxFrames> reveal_{};
    std::array<VkDescriptorSet, kMaxFrames> composite_sets_{};

    // gather_fbs_[image * kMaxFrames + frame]
    std::vector<VkFramebuffer> gather_fbs_{};
    std::vector<VkFramebuffer> composite_fbs_{};
    uint32_t swap_count_ = 0;
};

} // namespace gfx
