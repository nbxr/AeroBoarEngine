#pragma once

#include "PassType.h"
#include <vector>
#include <vulkan/vulkan.h>

namespace core {
struct SubpassContext {

    // Attachment references for this sub‑pass (these are owned by the 
    // parent pass, but the sub‑pass has a convenient copy of them 
    // for use in the sub‑pass description
    std::vector<VkAttachmentReference> color_attachments{};
    std::vector<VkAttachmentReference> resolve_attachments{}; // only in final subpass
    std::vector<VkAttachmentReference> input_attachments{};
    VkAttachmentReference depth_stencil_attachment{VK_ATTACHMENT_UNUSED,
                                                   VK_IMAGE_LAYOUT_UNDEFINED};

    // Pipeline used in this subpass (still shares the global pipeline_layout)
    VkPipeline pipeline{VK_NULL_HANDLE};

    // Optional flags / hints
    bool writes_depth{true};
    bool blend_enabled{false};

    // // ------------------------------------------------------------
    // //   Sub‑pass description (belongs to this pass, but does NOT own
    // //   the render pass or the pass‑wide resources)
    // // ------------------------------------------------------------
    // VkSubpassDescription subpass_description{};
    // std::vector<VkSubpassDependency> subpass_dependencies{};

    // // Bind point and index – cheap values that are needed for sub‑pass indexing
    // VkPipelineBindPoint pipeline_bind_point;
    // uint32_t subpass_index;

    // // ------------------------------------------------------------
    // //   Resources that are *specific* to the sub‑pass
    // // ------------------------------------------------------------
    // // The parent pass already owns the descriptor set layout for the pass,
    // // but a sub‑pass may allocate its own descriptor pool and sets.
    // VkPipelineLayout pipeline_layout{
    //     VK_NULL_HANDLE}; // often the same as pass->pipeline_layout
    // VkPipeline pipeline{VK_NULL_HANDLE}; // optional per‑sub‑pass pipeline
    // std::vector<VkDescriptorSet>
    //     descriptor_sets; // per‑sub‑pass descriptor sets
    // VkCommandPool command_pool{
    //     VK_NULL_HANDLE}; // dedicated pool for sub‑pass commands
    // std::vector<VkCommandBuffer>
    //     command_buffers{}; // command buffers for this sub‑pass
    // VkViewport
    //     viewport{}; // viewport for this sub‑pass (can be different per layer)
    // VkRect2D scissor{};            // scissor for this sub‑pass
    // VkExtent2D swapchain_extent{}; // may be XR layer extent
    // VkFormat swapchain_format{
    //     VK_FORMAT_UNDEFINED}; // copy of the pass‑wide format for safety
    // VkSampleCountFlagBits sample_count{VK_SAMPLE_COUNT_1_BIT};
};
} // namespace core
