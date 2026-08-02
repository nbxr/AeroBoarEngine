

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
    // One bindless descriptor set per frame in flight. Static data (SSBOs, textures) is bound
    // to all of them at load time. Per-frame data (globals UBO at binding 0) is updated only on
    // its own set in render(). This prevents descriptor updates on a shared set while another
    // in-flight frame's command buffer is still executing (a source of GPU-AV internal errors
    // and DEVICE_LOST on the second submit).
    std::vector<VkDescriptorSet> bindless_descriptor_sets{};
    
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
    uint32_t swap_chain_image_count = 0;   // Actual images returned by driver (can be > requested min)

    VkPipeline pipeline{VK_NULL_HANDLE};
    VkPushConstantRange push_constant_range{
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, // stageFlags
        0, // offset
        80 // Instanced PBR: mat4 viewProj (64) + uvec4 extra (16) = 80
    };

    // Semaphores are sized to the number of swapchain images (not MAX_FRAMES_IN_FLIGHT)
    // This is the robust way to handle drivers that return more images than requested.
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    
    bool enable_validation_layers{true};

    // GPU-Assisted Validation does heavy shader instrumentation. It is excellent
    // for catching bad bindless accesses, invalid image sampling, etc., but it
    // can be slow and (rarely) the layer itself can crash hard on certain app bugs
    // instead of printing a nice message. Set to false temporarily when the
    // validation layer itself is access-violating so you can see the actual errors.
    bool enable_gpu_assisted_validation{false};  // Set from configuration.json at instance creation.

    bool use_descriptor_heap{false};

    // Set to true if we ever receive VK_ERROR_DEVICE_LOST (or equivalent fatal
    // result). The main loop can observe this and stop spinning (which would
    // otherwise cause the validation callback + log file to be flooded with
    // millions of repeated errors).
    bool device_lost{false};
};
}; // namespace gfx