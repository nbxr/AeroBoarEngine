#pragma once

#include "gfx/AllocatedBuffer.h"
#include "gfx/AllocatedImage.h"
#include "gfx/PassType.h"
#include "gfx/SubpassContext.h"
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace gfx {
struct PassContext {
    VkRenderPass render_pass{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers{}; // one per swapchain image (or XR layer)

    // Attachment descriptions - shared by the pass and all its sub-passes
    std::vector<VkAttachmentDescription> attachments{};
    
    // Subpasses (opaque + transparent/post)
    std::vector<SubpassContext> subpasses{};
    
    // Shared resources for whole pass
    VkViewport viewport{};
    VkRect2D scissor{};
    VkSampleCountFlagBits sample_count{VK_SAMPLE_COUNT_1_BIT}; // 1, 2, or 4
    
    // Transient / on-chip friendly images - one set per swapchain image so that
    // multiple frames in flight (using different acquired swap images) can have
    // their render passes execute without overlapping on the same transient
    // attachment images. Sharing them worked for 1 frame in flight but caused
    // DEVICE_LOST when 2 frames' graphics work overlapped on the shared MSAA
    // color and depth (both use the same views in their framebuffers).
    std::vector<AllocatedImage> msaa_color_images{};
    std::vector<AllocatedImage> depth_images{};

    // Optional: fixed foveated density map
    AllocatedImage fdm_image{};
};
} // namespace gfx