#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include "core/AABB.h"
#include "gfx/AllocatedImage.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace gfx {

static constexpr uint32_t kShadowCascades = 3;
static constexpr uint32_t kShadowSilhouettePlanes = 8;

// Directional CSM (texture2D array). Written then sampled in the same command
// buffer (not per-FIF, not per-eye). VR later: both eyes sample this.
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

    static bool frustum_corners(const glm::mat4& view_proj, glm::vec3 out[8]);
    static void slice_frustum_corners(const glm::vec3 full[8], float t0, float t1,
                                      glm::vec3 out[8]);
    [[nodiscard]] static core::AABB caster_bounds_from_corners(
        const glm::vec3 corners[8], const glm::vec3& to_light,
        const glm::vec3& scene_min, const glm::vec3& scene_max);
    // Light-aligned clip planes from receiver-pyramid silhouette (Aaltonen).
    // Returns 0 if the hull is degenerate (looking along the light).
    static uint32_t silhouette_planes(const glm::vec3 corners[8],
                                      const glm::vec3& to_light, glm::vec4* out,
                                      uint32_t max_out);

    [[nodiscard]] static core::AABB camera_caster_bounds(
        const glm::mat4& view_proj, const glm::vec3& to_light,
        const glm::vec3& scene_min, const glm::vec3& scene_max);

    void prepare(VkCommandBuffer cmd, bool from_undefined = false);
    void begin(VkCommandBuffer cmd, uint32_t cascade);
    void end(VkCommandBuffer cmd);
    void finish(VkCommandBuffer cmd);

    [[nodiscard]] bool is_ready() const { return framebuffer_[0] != VK_NULL_HANDLE; }
    [[nodiscard]] uint32_t resolution() const { return resolution_; }
    [[nodiscard]] float texel_uv() const {
        return resolution_ > 0 ? 1.0f / static_cast<float>(resolution_) : 0.0f;
    }
    [[nodiscard]] VkPipeline pipeline() const { return pipeline_; }
    [[nodiscard]] VkRenderPass render_pass() const { return render_pass_; }

    bool enabled = true;
    bool last_on = false;
    float bias = 0.002f;
    glm::mat4 last_view_proj[kShadowCascades]{
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
    glm::vec4 last_splits{0.0f};
    glm::vec3 last_to_light{0.0f, 1.0f, 0.0f};
    uint32_t last_cascade_count = kShadowCascades;

  private:
    bool create_image(VkDevice device, VmaAllocator allocator, VkFormat format,
                      uint32_t w, uint32_t h, uint32_t layers, AllocatedImage& out,
                      VkImageViewType view_type, bool sampled) const;
    bool one_shot_clear(VkDevice device, VkQueue queue, VkCommandPool pool);

    uint32_t resolution_ = 0;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    AllocatedImage image_{};
    AllocatedImage dummy_{};
    VkImageView layer_view_[kShadowCascades]{};
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_[kShadowCascades]{};
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace gfx
