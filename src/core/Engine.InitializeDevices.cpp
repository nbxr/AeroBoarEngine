#include "Engine.h"
#include "Renderer.h"
#include "VkBootstrap.h"

vkb::Instance &core::Engine::init_vk_instance(core::Renderer &renderer,
                                              vkb::InstanceBuilder &builder) {
    // vulkan instance
    auto inst_ret =
        builder.set_app_name("AeroBoar")
            .request_validation_layers(renderer.vk.enable_validation_layers)
            .use_default_debug_messenger()
            .build();

    if (!inst_ret) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    renderer.vk.instance = inst_ret.value().instance;
    return inst_ret.value();
}

void core::Engine::add_features(vkb::PhysicalDeviceSelector &selector) {
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

std::pair<bool, vkb::PhysicalDevice>
core::Engine::init_physical_device(core::Renderer &renderer,
                                   vkb::Instance &inst) {
    // Add
    vkb::PhysicalDeviceSelector selector{inst};
    add_features(selector);
    auto phys_ret = selector.set_surface(renderer.vk.surface).select();

    if (!phys_ret) {
        LOG_ERROR("Failed to find suitable physical device");
        return {false, vkb::PhysicalDevice{}};
    }

    renderer.vk.physical_device = phys_ret.value().physical_device;
    return {true, phys_ret.value()};
}

std::pair<bool, vkb::Device>
core::Engine::init_logical_device(core::Renderer &renderer,
                                  vkb::PhysicalDevice &phys) {
    vkb::DeviceBuilder device_builder{phys};
    auto dev_ret = device_builder.build();
    if (!dev_ret) {
        LOG_ERROR("Failed to find suitable logical device");
        return {false, vkb::Device{}};
    }
    renderer.vk.device = dev_ret.value();
    return {true, dev_ret.value()};
}

bool core::Engine::init_graphics_queue(core::Renderer &renderer,
                                       vkb::Device &dev) {
    auto graphics_queue_ret = dev.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret) {
        LOG_ERROR("Failed to find graphics queue");
        return false;
    }
    VkQueue graphics_queue = graphics_queue_ret.value();
    renderer.vk.graphics_queue = graphics_queue;
    return true;
}

bool core::Engine::init_present_queue(core::Renderer &renderer,
                                      vkb::Device &dev) {
    auto present_queue_ret = dev.get_queue(vkb::QueueType::present);
    if (!present_queue_ret) {
        LOG_ERROR("Failed to find present queue");
        return false;
    }
    VkQueue present_queue = present_queue_ret.value();
    renderer.vk.present_queue = present_queue;
    return true;
}

bool core::Engine::init_transfer_queue(core::Renderer &renderer,
                                       vkb::Device &dev) {
    auto transfer_queue_ret = dev.get_queue(vkb::QueueType::transfer);
    if (!transfer_queue_ret) {
        // If no dedicated transfer queue, fall back to graphics queue
        transfer_queue_ret = dev.get_queue(vkb::QueueType::graphics);
        if (!transfer_queue_ret) {
            LOG_ERROR("Failed to find transfer queue or graphics queue");
            return false;
        }
    }
    VkQueue transfer_queue = transfer_queue_ret.value();
    renderer.vk.transfer_queue = transfer_queue;

    return true;
}

bool core::Engine::init_swapchain(core::Renderer &renderer, vkb::Device &dev) {
    vkb::SwapchainBuilder swapchain_builder{dev, renderer.vk.surface};
    auto swapchain_ret = swapchain_builder.build();

    if (!swapchain_ret) {
        LOG_ERROR("Failed to create swapchain");
        return false;
    }

    renderer.vk.swapchain = swapchain_ret.value();
    renderer.vk.swap_chain_image_format = renderer.vk.swapchain.image_format;
    renderer.vk.swap_chain_extent = renderer.vk.swapchain.extent;
    renderer.vk.swap_chain_images.resize(renderer.vk.swapchain.image_count);
    renderer.vk.swap_chain_image_views.resize(
        renderer.vk.swapchain.image_count);

    return true;
}

void core::Engine::recreate_swapchain(core::Renderer &renderer) {
    // recreate swapchain and related resources here
    vkb::SwapchainBuilder swapchain_builder{renderer.vk.device};
    auto swap_ret = swapchain_builder.set_old_swapchain(renderer.vk.swapchain).build();

    if (!swap_ret) {
        // If it failed to create a swapchain, the old swapchain handle is
        // invalid.
        renderer.vk.swapchain.swapchain = VK_NULL_HANDLE;
    } else {
        // Even though we recycled the previous swapchain, we need to free its
        // resources.
        vkb::destroy_swapchain(renderer.vk.swapchain);
        // Get the new swapchain and place it in our variable
        renderer.vk.swapchain = swap_ret.value();
    }
}
