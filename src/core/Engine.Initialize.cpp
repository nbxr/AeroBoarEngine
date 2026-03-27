#define VMA_IMPLEMENTATION

#include "Engine.h"
#include "AllocatedBuffer.h"
#include "AllocatedImage.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void core::Engine::initialize(core::Renderer &renderer) {
    init_vulkan(renderer);
    init_vma(renderer);
}

void core::Engine::init_vulkan(core::Renderer &renderer) {
    // Initialize Vulkan using vk-bootstrap
    vkb::InstanceBuilder builder{};

    // vulkan instance
    vkb::Instance inst = init_vk_instance(renderer, builder);

    // surface
    init_surface(renderer);

    // physical and logical devices
    vkb::PhysicalDevice phys = init_physical_device(renderer, inst);
    vkb::Device dev = init_logical_device(renderer, phys);

    // queues
    init_graphics_queue(renderer, dev);
    init_present_queue(renderer, dev);
    init_transfer_queue(renderer, dev);

    // swapchain
    init_swapchain(renderer, dev);

    // render pass
    init_render_pass(renderer);

    // Initialize descriptor pool and set layout for bindless rendering
    init_descriptor_pool(renderer);
    init_descriptor_set_layout(renderer);

    // Initialize pipeline layout
    init_pipeline_layout(renderer);

    // Initialize graphics pipeline
    init_graphics_pipeline(renderer);

    // Initialize command pool and buffers
    init_command_pool(renderer);
    init_command_buffers(renderer);

    // Initialize framebuffers
    init_framebuffers(renderer);

    // Initialize synchronization primitives
    init_sync_primitives(renderer);
}

void core::Engine::init_vma(core::Renderer &renderer) {
    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.instance = renderer.vk.instance;
    alloc_info.physicalDevice = renderer.vk.physical_device;
    alloc_info.device = renderer.vk.device;
    alloc_info.vulkanApiVersion = VK_API_VERSION_1_4;

    if (vmaCreateAllocator(&alloc_info, &renderer.allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan Memory Allocator");
    }
}

void core::Engine::init_surface(core::Renderer &renderer) {
    // surface
    if (glfwCreateWindowSurface(renderer.vk.instance,
                                renderer.window.glfw_handle, nullptr,
                                &renderer.vk.surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }
}
