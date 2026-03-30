#define VMA_IMPLEMENTATION
#include "Engine.h"
#include "AllocatedBuffer.h"
#include "AllocatedImage.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

bool core::Engine::initialize(core::Renderer &renderer) {
    if (init_vulkan(renderer) && init_vma(renderer) &&
        renderer.materialManager.Initialize(renderer.vk.device,
                                            renderer.allocator)) {
        return true;
    } else
        return false;
}

bool core::Engine::init_vulkan(core::Renderer &renderer) {
    // Initialize Vulkan using vk-bootstrap
    vkb::InstanceBuilder builder{};

    // vulkan instance
    vkb::Instance inst = init_vk_instance(renderer, builder);

    // surface
    init_surface(renderer);

    // physical and logical devices
    auto init_phys = init_physical_device(renderer, inst);
    if (!init_phys.first)
        return false;
    auto phys = init_phys.second;

    auto init_dev = init_logical_device(renderer, phys);
    if (!init_dev.first)
        return false;
    auto dev = init_dev.second;

    // queues
    if (!init_graphics_queue(renderer, dev))
        return false;
    if (!init_present_queue(renderer, dev))
        return false;
    if (!init_transfer_queue(renderer, dev))
        return false;

    // swapchain
    if (!init_swapchain(renderer, dev))
        return false;

    // render pass
    if (!init_render_pass(renderer))
        return false;

    // Initialize descriptor pool and set layout for bindless rendering
    if (!init_descriptor_pool(renderer))
        return false;
    if (!init_descriptor_set_layout(renderer))
        return false;

    // Initialize pipeline layout
    if (!init_pipeline_layout(renderer))
        return false;

    // Initialize graphics pipeline
    if (!init_graphics_pipeline(renderer))
        return false;

    // Initialize command pool and buffers
    if (!init_command_pool(renderer))
        return false;
    if (!init_command_buffers(renderer))
        return false;

    // Initialize framebuffers
    if (!init_framebuffers(renderer))
        return false;

    // Initialize synchronization primitives
    if (!init_sync_primitives(renderer))
        return false;

    return true;
}

bool core::Engine::init_vma(core::Renderer &renderer) {
    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.instance = renderer.vk.instance;
    alloc_info.physicalDevice = renderer.vk.physical_device;
    alloc_info.device = renderer.vk.device;
    alloc_info.vulkanApiVersion = VK_API_VERSION_1_4;

    if (vmaCreateAllocator(&alloc_info, &renderer.allocator) != VK_SUCCESS) {
        LOG_ERROR("Failed to create Vulkan Memory Allocator");
        return false;
    }

    return true;
}

bool core::Engine::init_surface(core::Renderer &renderer) {
    // surface
    if (glfwCreateWindowSurface(renderer.vk.instance,
                                renderer.window.glfw_handle, nullptr,
                                &renderer.vk.surface) != VK_SUCCESS) {
        LOG_ERROR("Failed to create Vulkan surface");
        return false;
    }
    return true;
}
