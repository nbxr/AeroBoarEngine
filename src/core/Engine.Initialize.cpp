#define VMA_IMPLEMENTATION

#include "Engine.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void Core::Engine::initialize(Renderer &renderer) {
    init_vulkan(renderer);
    init_vma(renderer);
}

inline void add_features(vkb::PhysicalDeviceSelector &selector) {
    // standard settings
    selector.set_minimum_version(1, 4).require_dedicated_transfer_queue();

    // enable bindless rendering
    selector.add_required_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.descriptorIndexing = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    features12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    selector.set_required_features_12(features12);

    // enable descriptor heap
    // Enable VK_EXT_descriptor_indexing extension features
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT featuresIndexing = {};
    featuresIndexing.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
    featuresIndexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    featuresIndexing.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    featuresIndexing.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    featuresIndexing.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    featuresIndexing.runtimeDescriptorArray = VK_TRUE;
    featuresIndexing.descriptorBindingVariableDescriptorCount = VK_TRUE;
    featuresIndexing.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    selector.add_required_extension_features(featuresIndexing);
}

void Core::Engine::init_vulkan(Renderer &renderer) {
    // Initialize Vulkan using vk-bootstrap
    vkb::InstanceBuilder builder{};

    // vulkan instance
    auto inst_ret =
        builder.set_app_name("AeroBoar")
            .request_validation_layers(renderer.vk.enable_validation_layers)
            .use_default_debug_messenger()
            .build();

    if (!inst_ret) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }
    renderer.vk.instance = inst_ret.value();

    // surface
    if (glfwCreateWindowSurface(renderer.vk.instance,
                                renderer.window.glfw_handle, nullptr,
                                &renderer.vk.surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }

    // physical device
    vkb::PhysicalDeviceSelector selector{renderer.vk.instance};
    add_features(selector);
    auto phys_ret = selector.set_surface(renderer.vk.surface).select();

    if (!phys_ret) {
        throw std::runtime_error("Failed to find suitable physical device");
    }
    renderer.vk.physical_device = phys_ret.value();

    // logical device
    vkb::DeviceBuilder device_builder{renderer.vk.physical_device};
    auto dev_ret = device_builder.build();
    if (!dev_ret) {
        throw std::runtime_error("Failed to find suitable logical device");
    }
    renderer.vk.device = dev_ret.value();

    // graphics queue
    auto graphics_queue_ret =
        renderer.vk.device.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret) {
        throw std::runtime_error("Failed to find graphics queue");
    }
    VkQueue graphics_queue = graphics_queue_ret.value();
    renderer.vk.graphics_queue = graphics_queue;
}

void Core::Engine::init_vma(Renderer &renderer) {
    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.instance = renderer.vk.instance;
    alloc_info.physicalDevice = renderer.vk.physical_device;
    alloc_info.device = renderer.vk.device;
    alloc_info.vulkanApiVersion = VK_API_VERSION_1_4;

    if (vmaCreateAllocator(&alloc_info, &renderer.allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan Memory Allocator");
    }
}
