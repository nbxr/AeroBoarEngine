#pragma once

#include "gfx/AllocatedImage.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace gfx {

// Hierarchical-Z (RG: min + max depth) pyramid for **same-frame** occlusion.
// Reverse-Z: conservative cull uses R (min = far/hole).
// Built after a depth prepass at the current pose; cull immediately after
// in the same command buffer. Double-buffered for frames-in-flight so
// concurrent submissions do not stomp each other's pyramid.
//
// Cull must use the **same** view_proj that rendered the prepass depth.
// No camera-stability hysteresis (VR-ready).
class HzbPyramid {
  public:
    static constexpr uint32_t kMaxFrames = 2;
    static constexpr uint32_t kMaxMips = 16;

    bool initialize(VkDevice device, VmaAllocator allocator);
    void destroy(VkDevice device, VmaAllocator allocator);

    bool resize(VkDevice device, VmaAllocator allocator, VkExtent2D extent);

    // Wire prepass depth into copy descriptors. Call when idle (init / resize),
    // never while a command buffer that used these sets is still recording.
    void bind_depth_source(uint32_t frame_index, VkImageView depth_view);

    // UNDEFINED → GENERAL for every pyramid slot (record into a one-shot CB).
    // Cull samples HZB in GENERAL; must run after resize before first frame.
    // No-op if layouts were already initialized for this pyramid generation.
    void record_init_layouts(VkCommandBuffer cmd);
    [[nodiscard]] bool needs_layout_init() const { return needs_layout_init_; }

    // After depth prepass: min/max downsample full-res depth into mip0, reduce mips.
    // Descriptors must already be bound (bind_depth_source + create_images).
    // Pyramid stays in GENERAL for sampling (matches bind_hzb layout).
    void record_build(VkCommandBuffer cmd, uint32_t frame_index, VkExtent2D depth_extent);

    [[nodiscard]] VkImageView full_view(uint32_t frame_index) const {
        return full_views_[frame_index];
    }
    [[nodiscard]] VkSampler sampler() const { return sampler_; }

    // Resources allocated (extent valid). Pyramid is valid after record_build
    // in the same frame before occlusion cull.
    [[nodiscard]] bool is_ready() const { return ready_; }
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

    std::array<VkDescriptorSet, kMaxFrames> copy_sets_{};
    std::array<std::vector<VkDescriptorSet>, kMaxFrames> reduce_sets_{};

    std::array<AllocatedImage, kMaxFrames> images_{};
    std::array<VkImageView, kMaxFrames> full_views_{};
    std::array<std::vector<VkImageView>, kMaxFrames> mip_views_{};

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t mip_count_ = 0;
    bool ready_ = false;
    // Set by resize(); cleared after record_init_layouts().
    bool needs_layout_init_ = false;
};

} // namespace gfx
