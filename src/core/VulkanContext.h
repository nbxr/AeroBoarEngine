

#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include "VkBootstrap.h"

namespace core {
struct PassContext;
struct VulkanContext {
    // Core Vulkan objects (created once)
    VkInstance instance;
    VkPhysicalDevice physical_device;
    vkb::Device device{};
    vkb::Swapchain swapchain{};

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

    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPushConstantRange push_constant_range{
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, // stageFlags
        0, // offset
        64 // size (or smaller — keep under 128 bytes for Quest 3)
    };
    
    bool enable_validation_layers{false};
    bool use_descriptor_heap{false}; // For runtime switch between descriptor indexing and heap

    // // Integer types    
    // VkSurfaceFormatKHR surface_format;
    // VkPresentModeKHR present_mode;

    // // Formats
    // VkFormat depth_format;

    // // Vectors (these have pointer overhead)
    // std::vector<VkCommandBuffer> command_buffers;
    // std::vector<VkImage> swap_chain_images;
    // std::vector<VkImageView> swap_chain_image_views;

    // // Synchronization
    // std::vector<VkSemaphore> image_available_semaphores;
    // std::vector<VkSemaphore> render_finished_semaphores;
    // std::vector<VkFence> in_flight_fences;
 
    // // Validation layers and required extensions
    // std::vector<const char *> validation_layers;
    // std::vector<const char *> required_extensions;
};
}; // namespace Core