

#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include "VkBootstrap.h"

namespace gfx {
struct PassContext;
struct VulkanContext {
    // Core Vulkan objects (created once)
    vkb::Instance instance;
    VkPhysicalDevice physical_device;
    vkb::Device device{};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    
    VkQueue graphics_queue{VK_NULL_HANDLE};
    VkQueue present_queue{VK_NULL_HANDLE};
    VkQueue transfer_queue{VK_NULL_HANDLE};
    
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkCommandPool generic_command_pool{VK_NULL_HANDLE}; // one-time submissions / transfer work

    // Bindless + pipeline (shared by everything)    
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptor_set_layout{VK_NULL_HANDLE};
    VkDescriptorSet bindless_descriptor_set{VK_NULL_HANDLE};
    
    // Basic info
    uint32_t graphics_family_index{0};
    uint32_t present_family_index{0};
    uint32_t transfer_family_index{0};

    VkFormat swap_chain_image_format{VK_FORMAT_UNDEFINED};
    VkFormat depth_format{VK_FORMAT_UNDEFINED};
    VkExtent2D swap_chain_extent{0, 0};
    VkSampleCountFlagBits msaa_color{VK_SAMPLE_COUNT_4_BIT};
    VkSampleCountFlagBits msaa_depth{VK_SAMPLE_COUNT_4_BIT};
    std::vector<VkImageView> swap_chain_image_views{};

    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPushConstantRange push_constant_range{
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, // stageFlags
        0, // offset
        160 // Phase 1 lighting: 2x mat4 (128) + uvec4 (16) + vec4 cameraPos (16) = 160
    };

    // Semaphores are sized to the number of swapchain images (not MAX_FRAMES_IN_FLIGHT)
    // This is the robust way to handle drivers that return more images than requested.
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    
    bool enable_validation_layers{true};
    bool use_descriptor_heap{false};
};
}; // namespace gfx