#pragma once

#include "AllocatedBuffer.h"
#include "AllocatedImage.h"
#include "PassType.h"
#include "SubpassContext.h"
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace core {
struct PassContext {
    VkRenderPass render_pass{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers{}; // one per swapchain image (or XR layer)

    // Attachment descriptions - shared by the pass and all its sub-passes
    std::vector<VkAttachmentDescription> attachments{};
    
    // Subpasses (opaque + transparent/post)
    std::vector<core::SubpassContext> subpasses{};
    
    // Shared resources for whole pass
    VkViewport viewport{};
    VkRect2D scissor{};
    VkSampleCountFlagBits sample_count{VK_SAMPLE_COUNT_1_BIT}; // 1, 2, or 4
    
    // Transient / on-chip firendly images
    core::AllocatedImage msaa_color_image{};
    core::AllocatedImage resolved_color_image{};    // final store target
    core::AllocatedImage depth_image{};             // for non-MSAA passes or depth resolve

    // Optional: fixed foveated density map
    core::AllocatedImage fdm_image{};

    // VkAttachmentReference color_attachment_refs[2]; // For two views in multiview
    // uint32_t view_mask;           // For multiview support
    // PassType type;

    // // Additional pass-specific fields that might be needed
    // VkCommandPool command_pool{VK_NULL_HANDLE};         // pool for this pass
    // std::vector<VkCommandBuffer> command_buffers{};     // allocated from this pool
    // VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};   // pool that can allocate many descriptors
    // std::vector<VkDescriptorSet> descriptor_sets{};     // per-frame descriptor sets

    // core::AllocatedBuffer vertex_buffer{};
    // core::AllocatedBuffer index_buffer{};
    // core::AllocatedBuffer uniform_buffer{};
    // core::AllocatedBuffer storage_buffer{};
    // core::AllocatedImage depth_image{};
    // core::AllocatedImage color_image{};
    // VkSampler sampler{VK_NULL_HANDLE};
    // 
    // VkExtent2D swapchain_extent{};
    // VkFormat swapchain_format{VK_FORMAT_UNDEFINED};
    // VkPushConstantRange push_constant_range{};
};
} // namespace core