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
    std::vector<VkFramebuffer> framebuffers{};
    VkPipeline pipeline{VK_NULL_HANDLE};

    // Pipeline & descriptor sets (bindless)
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptor_set_layout{VK_NULL_HANDLE};

    // Attachment descriptions - shared by the pass and all its sub-passes
    std::vector<VkAttachmentDescription> color_attachments{};
    std::vector<VkAttachmentDescription> depth_attachments{};
    VkAttachmentReference
        color_attachment_refs[2]; // For two views in multiview
    uint32_t view_mask;           // For multiview support
    PassType type;

    // Additional pass-specific fields that might be needed
    VkCommandPool command_pool{VK_NULL_HANDLE};         // generic pool for this pass
    std::vector<VkCommandBuffer> command_buffers{};     // allocated from this pool
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};   // pool that can allocate many descriptors
    std::vector<VkDescriptorSet> descriptor_sets{};     // per-frame descriptor sets

    std::unordered_map<core::PassType, core::SubpassContext> subpasses{};

    core::AllocatedBuffer vertex_buffer{};
    core::AllocatedBuffer index_buffer{};
    core::AllocatedBuffer uniform_buffer{};
    core::AllocatedBuffer storage_buffer{};
    core::AllocatedImage depth_image{};
    core::AllocatedImage color_image{};
    VkSampler sampler{VK_NULL_HANDLE};
    VkViewport viewport{};
    VkRect2D scissor{};
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format{VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits sample_count{VK_SAMPLE_COUNT_1_BIT};
    VkPushConstantRange push_constant_range{};
};
} // namespace core