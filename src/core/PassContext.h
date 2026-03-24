#pragma once

#include <vector>
#include <vulkan/vulkan.h>

namespace core {
struct PassContext {
    VkRenderPass render_pass;
    std::vector<VkFramebuffer> framebuffers;
    // Add other pass-specific data here
    // For example:
    // VkPipeline pipeline;
    // VkDescriptorSetLayout descriptor_set_layout;
    // etc.
};
};