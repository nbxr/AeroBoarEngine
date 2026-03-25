#pragma once

#include "PassType.h"
#include <vector>
#include <vulkan/vulkan.h>
#include <unordered_map>
#include "AllocatedBuffer.h"
#include "AllocatedImage.h"
#include "SubpassContext.h"

namespace core {
struct PassContext {
    VkRenderPass render_pass;
    std::vector<VkFramebuffer> framebuffers;
    VkPipeline pipeline;

    // Pipeline & descriptor sets (bindless)
    VkPipelineLayout pipeline_layout;
    VkDescriptorSet descriptor_set;
    VkDescriptorSetLayout descriptor_set_layout;

    // Attachment descriptions - shared by the pass and all its sub-passes
    std::vector<VkAttachmentDescription> color_attachments;
    std::vector<VkAttachmentDescription> depth_attachments;
    VkAttachmentReference
        color_attachment_refs[2]; // For two views in multiview
    uint32_t view_mask;           // For multiview support
    PassType type;

    // Additional pass-specific fields that might be needed
    VkCommandPool command_pool;                     // generic pool for this pass
    std::vector<VkCommandBuffer> command_buffers;   // allocated from this pool
    VkDescriptorPool descriptor_pool;               // pool that can allocate many descriptors
    std::vector<VkDescriptorSet> descriptor_sets;   // per-frame descriptor sets
    
    std::unordered_map<core::PassType, core::SubpassContext> subpasses{};
    
    core::AllocatedBuffer vertex_buffer;
    core::AllocatedBuffer index_buffer;
    core::AllocatedBuffer uniform_buffer;
    core::AllocatedBuffer storage_buffer;
    core::AllocatedImage depth_image;
    core::AllocatedImage color_image;
    VkSampler sampler;
    VkViewport viewport;
    VkRect2D scissor;
    VkExtent2D swapchain_extent;
    VkFormat swapchain_format;
    VkSampleCountFlagBits sample_count;
};
} // namespace core