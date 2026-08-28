#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include "gfx/AllocatedImage.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace gfx {

// One directional light-space shadow map. Written then sampled in the same
// command buffer (not per-FIF, not per-eye). VR later: both eyes sample this.
class ShadowMap {
  public:
    bool create(VkDevice device, VmaAllocator allocator, VkFormat depth_format,
                uint32_t resolution, VkQueue graphics_queue, VkCommandPool pool);
    void destroy(VkDevice device, VmaAllocator allocator);

    void bind_descriptor(VkDevice device, VkDescriptorSet set, uint32_t binding) const;

    // Tight reverse-Z ortho around world AABB corners, texel-snapped.
    // `to_light` is the NdotL vector (KHR directional world +Z).
    [[nodiscard]] glm::mat4 fit_view_proj(const glm::vec3& to_light,
                                          const glm::vec3& aabb_min,
                                          const glm::vec3& aabb_max) const;

    void begin(VkCommandBuffer cmd);
    void end(VkCommandBuffer cmd);

    [[nodiscard]] bool is_ready() const { return framebuffer_ != VK_NULL_HANDLE; }
    [[nodiscard]] uint32_t resolution() const { return resolution_; }
    [[nodiscard]] float texel_uv() const {
        return resolution_ > 0 ? 1.0f / static_cast<float>(resolution_) : 0.0f;
    }
    [[nodiscard]] VkPipeline pipeline() const { return pipeline_; }
    [[nodiscard]] VkRenderPass render_pass() const { return render_pass_; }

    bool enabled = true;
    bool last_on = false;
    float bias = 0.002f;
    glm::mat4 last_view_proj{1.0f};

  private:
    bool create_image(VkDevice device, VmaAllocator allocator, VkFormat format,
                      uint32_t w, uint32_t h, AllocatedImage& out,
                      bool sampled) const;
    bool one_shot_clear(VkDevice device, VkQueue queue, VkCommandPool pool);

    uint32_t resolution_ = 0;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    AllocatedImage image_{};
    AllocatedImage dummy_{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace gfx
