#define VMA_IMPLEMENTATION
#include "Engine.h"
#include "AllocatedBuffer.h"
#include "AllocatedImage.h"
#include "GameObject.h"
#include "RenderMesh.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

bool core::Engine::initialize() {
    if (init_vulkan()) {
        return true;
    } else
        return false;
}

bool core::Engine::init_vulkan() {
    // Initialize Vulkan using vk-bootstrap
    vkb::InstanceBuilder builder{};

    // vulkan instance
    if (!init_vk_instance(builder)) // destroy_devices
        return false;

    // surface
    init_surface(); // destroy_devices

    // physical and logical devices
    auto init_phys = init_physical_device(); // n/a
    if (!init_phys.first)
        return false;
    auto phys = init_phys.second;

    select_depth_format(phys);

    auto init_dev = init_logical_device(phys); // destroy_devices

    if (!init_dev.first)
        return false;

    auto dev = init_dev.second;

    if (!init_vma()) // destroy_vma
        return false;

    // queues
    if (!init_graphics_queue(dev)) // n/a
        return false;

    if (!init_present_queue(dev)) // n/a
        return false;

    if (!init_transfer_queue(dev)) // n/a
        return false;

    // swapchain
    if (!init_swapchain(dev)) // destroy_swapchain
        return false;

    // render pass
    if (!init_render_pass()) //
        return false;

    // intialize MSAA and depth images
    if (!init_msaa_color_image())
        return false;

    if (!init_depth_image())
        return false;

    // Initialize descriptor pool and set layout for bindless rendering
    if (!init_descriptor_pool())
        return false;
    if (!init_descriptor_set_layout())
        return false;

    // Initialize pipeline layout
    if (!init_pipeline_layout())
        return false;

    // Initialize graphics pipeline
    if (!init_graphics_pipeline())
        return false;

    // Initialize command pool and buffers
    if (!init_command_pool())
        return false;

    if (!init_command_buffers())
        return false;

    // Initialize framebuffers
    if (!init_framebuffers())
        return false;

    // Initialize synchronization primitives
    if (!init_sync_primitives())
        return false;

    if (!init_resource_managers())
        return false;

    return true;
}

bool core::Engine::init_vma() {
    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.instance = renderer.vk.instance;
    alloc_info.physicalDevice = renderer.vk.physical_device;
    alloc_info.device = renderer.vk.device.device;
    alloc_info.vulkanApiVersion = VK_API_VERSION_1_4;
    alloc_info.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT |
                       VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
                       VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    if (vmaCreateAllocator(&alloc_info, &renderer.allocator) != VK_SUCCESS) {
        LOG_ERROR("Failed to create Vulkan Memory Allocator");
        return false;
    }

    return true;
}

bool core::Engine::init_surface() {
    // surface
    if (glfwCreateWindowSurface(renderer.vk.instance,
                                renderer.window.glfw_handle, nullptr,
                                &renderer.vk.surface) != VK_SUCCESS) {
        LOG_ERROR("Failed to create Vulkan surface");
        return false;
    }
    return true;
}

bool core::Engine::init_resource_managers() {
    if (!renderer.scene_manager.initialize(
            renderer.vk.device.device, renderer.allocator,
            renderer.vk.bindless_descriptor_set, 100))
        return false;

    if (!renderer.material_manager.initialize(
            renderer.vk.device.device, renderer.allocator,
            renderer.vk.bindless_descriptor_set, 100))
        return false;

    if (!renderer.mesh_manager.initialize(
            renderer.vk.device.device, renderer.allocator,
            renderer.vk.bindless_descriptor_set, 100))
        return false;

    if (!renderer.texture_manager.initialize(
            renderer.vk.device.device, renderer.allocator,
            renderer.vk.transfer_queue, renderer.vk.graphics_queue,
            renderer.vk.graphics_family_index,
            renderer.vk.transfer_family_index,
            renderer.vk.bindless_descriptor_set))
        return false;

    return true;
}
