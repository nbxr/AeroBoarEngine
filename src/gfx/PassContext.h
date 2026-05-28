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
    
    // Transient / on-chip friendly images
    AllocatedImage msaa_color_image{};
    AllocatedImage resolved_color_image{};    // final store target
    AllocatedImage depth_image{};             // for non-MSAA passes or depth resolve

    // Optional: fixed foveated density map
    AllocatedImage fdm_image{};
};
} // namespace gfx