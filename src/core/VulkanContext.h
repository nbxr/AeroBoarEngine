#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include "VkBootstrap.h"

namespace core {
struct VulkanContext {
    // Largest types first to minimize padding
    vkb::Instance instance;
    vkb::PhysicalDevice physical_device;
    vkb::Device device;
    vkb::Swapchain swapchain;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue transfer_queue;
    VkSurfaceKHR surface;
    VkCommandPool command_pool;
    VkRenderPass render_pass;

    // Integer types
    uint32_t graphics_family_index;
    uint32_t present_family_index;
    VkFormat swap_chain_image_format;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;

    // Extent structures
    VkExtent2D swap_chain_extent;

    // Vectors (these have pointer overhead)
    std::vector<VkCommandBuffer> command_buffers;
    std::vector<VkImage> swap_chain_images;
    std::vector<VkImageView> swap_chain_image_views;

    // Synchronization
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;

    // Validation layers and required extensions
    std::vector<const char *> validation_layers;
    std::vector<const char *> required_extensions;

    // Booleans
    bool enable_validation_layers;
};
}; // namespace Core
