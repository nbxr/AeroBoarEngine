#pragma once

#include "PassType.h"
#include <vector>
#include <vulkan/vulkan.h>

namespace core {
struct SubpassContext {

    // ------------------------------------------------------------
    //   Sub‑pass description (belongs to this pass, but does NOT own
    //   the render pass or the pass‑wide resources)
    // ------------------------------------------------------------
    VkSubpassDescription subpass_description;
    std::vector<VkSubpassDependency> subpass_dependencies;
    std::vector<VkAttachmentReference> input_attachments;
    std::vector<VkAttachmentReference> color_attachments;
    std::vector<VkAttachmentReference> resolve_attachments;
    VkAttachmentReference depth_stencil_attachment;

    // Bind point and index – cheap values that are needed for sub‑pass indexing
    VkPipelineBindPoint pipeline_bind_point;
    uint32_t subpass_index;

    // ------------------------------------------------------------
    //   Resources that are *specific* to the sub‑pass
    // ------------------------------------------------------------
    // The parent pass already owns the descriptor set layout for the pass,
    // but a sub‑pass may allocate its own descriptor pool and sets.
    VkPipelineLayout pipeline_layout; // often the same as pass->pipeline_layout
    VkPipeline pipeline;              // optional per‑sub‑pass pipeline
    std::vector<VkDescriptorSet>
        descriptor_sets;        // per‑sub‑pass descriptor sets
    VkCommandPool command_pool; // dedicated pool for sub‑pass commands
    std::vector<VkCommandBuffer>
        command_buffers; // command buffers for this sub‑pass

    VkViewport
        viewport;     // viewport for this sub‑pass (can be different per layer)
    VkRect2D scissor; // scissor for this sub‑pass
    VkExtent2D swapchain_extent; // may be XR layer extent
    VkFormat swapchain_format;   // copy of the pass‑wide format for safety
    VkSampleCountFlagBits sample_count;
};
} // namespace core
