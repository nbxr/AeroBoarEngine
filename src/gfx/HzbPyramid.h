#pragma once

#include "gfx/AllocatedImage.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace gfx {

// Hierarchical-Z (min-depth) pyramid for previous-frame occlusion culling.
// Built after the main pass from the single-sample resolved depth.
// Double-buffered: slot f is written at end of frame f and read after that
// frame's fence is waited (next time slot f is used).
//
// Occlusion tests MUST project AABBs with view_proj_for() of that slot — the
// same matrix used when the depth was rendered — otherwise depth values are
// not comparable and objects false-cull.
//
// Desktop hysteresis (should_use_occlusion): hard-off on camera motion, re-enable
// only after kMinStableFrames of stillness. Intentional interim for mouse-look;
// improve for VR (same-frame HZB or reprojection) — see docs/agents/tech_context.md.
class HzbPyramid {
  public:
    static constexpr uint32_t kMaxFrames = 2;
    static constexpr uint32_t kMaxMips = 16;

    bool initialize(VkDevice device, VmaAllocator allocator);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Recreate pyramids for the current swapchain extent. Call on init + resize.
    bool resize(VkDevice device, VmaAllocator allocator, VkExtent2D extent);

    // After main pass: sample resolved depth into mip0, reduce remaining mips.
    // depth_view must be single-sample depth, layout SHADER_READ_ONLY_OPTIMAL.
    // view_proj must be the exact matrix used for the main pass that wrote depth.
    // cam_pos / cam_forward are used to gate occlusion when the camera has moved.
    void record_build(VkCommandBuffer cmd, uint32_t frame_index, VkImageView depth_view,
                      VkImage depth_image, VkExtent2D depth_extent,
                      const glm::mat4& view_proj, const glm::vec3& cam_pos,
                      const glm::vec3& cam_forward);

    // Bindable for cull compute: combined sampler over full mip chain (R32F).
    [[nodiscard]] VkImageView full_view(uint32_t frame_index) const {
        return full_views_[frame_index];
    }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }

    // View-projection used when this slot's depth/HZB was generated.
    [[nodiscard]] const glm::mat4& view_proj_for(uint32_t frame_index) const {
        return view_proj_[frame_index];
    }

    // Hard occlusion for this frame? Combines:
    //  - inter-frame motion hysteresis (mouse-look → off immediately; must sit
    //    still for kMinStableFrames before on again),
    //  - capture-camera compatibility for the HZB slot (depth-space match).
    // bias_scale_out: >1 for the first frames after re-enable (safer / less pop).
    [[nodiscard]] bool should_use_occlusion(uint32_t frame_index,
                                            const glm::vec3& cam_pos,
                                            const glm::vec3& cam_forward,
                                            float* bias_scale_out = nullptr);

    [[nodiscard]] bool is_ready(uint32_t frame_index) const {
        return ready_ && built_[frame_index];
    }
    [[nodiscard]] uint32_t mip_count() const { return mip_count_; }
    [[nodiscard]] uint32_t width() const { return width_; }
    [[nodiscard]] uint32_t height() const { return height_; }

  private:
    bool create_pipelines(VkDevice device);
    bool create_images(VkDevice device, VmaAllocator allocator, uint32_t frame);
    void destroy_images(VkDevice device, VmaAllocator allocator, uint32_t frame);

    VkDevice device_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline copy_pipeline_ = VK_NULL_HANDLE;
    VkPipeline reduce_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkSampler depth_sampler_ = VK_NULL_HANDLE;

    // One set for copy + (mip_count-1) reduce sets per frame (allocated on demand)
    std::array<VkDescriptorSet, kMaxFrames> copy_sets_{};
    std::array<std::vector<VkDescriptorSet>, kMaxFrames> reduce_sets_{};

    std::array<AllocatedImage, kMaxFrames> images_{};
    std::array<VkImageView, kMaxFrames> full_views_{};
    std::array<std::vector<VkImageView>, kMaxFrames> mip_views_{};
    std::array<bool, kMaxFrames> built_{};
    std::array<glm::mat4, kMaxFrames> view_proj_{};
    std::array<glm::vec3, kMaxFrames> cam_pos_{};
    std::array<glm::vec3, kMaxFrames> cam_forward_{};

    // Hysteresis: inter-frame camera tracking (not the HZB capture camera).
    // Instant OFF on motion; long settle before ON — mid-look HZB is what pops.
    static constexpr uint32_t kMinStableFrames = 24; // ~400 ms at 60 Hz
    static constexpr uint32_t kWarmupFrames = 30;    // extra-conservative bias after ON
    glm::vec3 last_frame_pos_{0.0f};
    glm::vec3 last_frame_forward_{0.0f, 0.0f, -1.0f};
    bool have_last_frame_cam_ = false;
    uint32_t stable_frames_ = 0;

    [[nodiscard]] bool capture_compatible(uint32_t frame_index, const glm::vec3& cam_pos,
                                          const glm::vec3& cam_forward) const;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t mip_count_ = 0;
    bool ready_ = false;
};

} // namespace gfx
