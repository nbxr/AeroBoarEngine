#include "Engine.h"
#include "Renderer.h"
#include "VkBootstrap.h"

bool core::Engine::init_vk_instance(vkb::InstanceBuilder &builder) {
    // vulkan instance
    auto inst_ret =
        builder.set_app_name("AeroBoar")
            .require_api_version(1, 4)
            .request_validation_layers(renderer.vk.enable_validation_layers)
            .use_default_debug_messenger()
            .build();

    if (!inst_ret) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    renderer.vk.instance = inst_ret.value();
    return true;
}

void core::Engine::add_features(vkb::PhysicalDeviceSelector &selector) {
    // standard settings
    selector.set_minimum_version(1, 4).require_dedicated_transfer_queue();

    // enable bindless rendering
    selector.add_required_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    selector.add_required_extension(
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);

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
    features12.bufferDeviceAddress = VK_TRUE;
    selector.set_required_features_12(features12);
}

std::pair<bool, vkb::PhysicalDevice> core::Engine::init_physical_device() {
    // Add
    auto &inst = renderer.vk.instance;
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
core::Engine::init_logical_device(vkb::PhysicalDevice &phys) {
    vkb::DeviceBuilder device_builder{phys};
    auto dev_ret = device_builder.build();
    if (!dev_ret) {
        LOG_ERROR("Failed to find suitable logical device");
        return {false, vkb::Device{}};
    }
    renderer.vk.device = dev_ret.value();
    return {true, dev_ret.value()};
}

void core::Engine::select_depth_format(vkb::PhysicalDevice &phys) {
    VkFormat formats[] = {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT,
                          VK_FORMAT_D24_UNORM_S8_UINT};

    for (auto format : formats) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(phys, format, &props);

        if (props.optimalTilingFeatures &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            renderer.vk.depth_format = format;
            return;
        }
    }

    renderer.vk.depth_format = VK_FORMAT_D32_SFLOAT;
}

bool core::Engine::init_graphics_queue(vkb::Device &dev) {
    auto graphics_queue_ret = dev.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret) {
        LOG_ERROR("Failed to find graphics queue");
        return false;
    }
    VkQueue graphics_queue = graphics_queue_ret.value();
    renderer.vk.graphics_queue = graphics_queue;
    renderer.vk.graphics_family_index = 0;
    return true;
}

bool core::Engine::init_present_queue(vkb::Device &dev) {
    auto present_queue_ret = dev.get_queue(vkb::QueueType::present);
    if (!present_queue_ret) {
        LOG_ERROR("Failed to find present queue");
        return false;
    }
    VkQueue present_queue = present_queue_ret.value();
    renderer.vk.present_queue = present_queue;
    renderer.vk.present_family_index = 0;
    return true;
}

bool core::Engine::init_transfer_queue(vkb::Device &dev) {
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
    renderer.vk.transfer_family_index =
        dev.get_queue_index(vkb::QueueType::transfer).value();

    return true;
}

bool core::Engine::init_swapchain(vkb::Device &dev) {
    vkb::SwapchainBuilder swapchain_builder{dev, renderer.vk.surface};
    auto swap_ret =
        swapchain_builder
            .set_desired_min_image_count(renderer.MAX_FRAMES_IN_FLIGHT)
            .build();

    if (!swap_ret) {
        LOG_ERROR("Failed to create swapchain");
        return false;
    }

    renderer.vk.swapchain = swap_ret.value().swapchain;
    renderer.vk.swap_chain_image_format = swap_ret.value().image_format;
    renderer.vk.swap_chain_extent = swap_ret.value().extent;

    // Store views once (vkb creates new each get_image_views call)
    auto views_res = swap_ret.value().get_image_views();
    if (!views_res) {
        LOG_ERROR("Failed to get swapchain image views");
        return false;
    }

    renderer.vk.swap_chain_image_views = views_res.value();

    return true;
}

void core::Engine::recreate_swapchain() {
    vkDeviceWaitIdle(renderer.vk.device);

    // destroy old image views
    for (auto &view : renderer.vk.swap_chain_image_views) {
        vkDestroyImageView(renderer.vk.device, view, nullptr);
    }
    renderer.vk.swap_chain_image_views.clear();

    // recreate swapchain and related resources here
    vkb::SwapchainBuilder swapchain_builder{renderer.vk.device};
    auto swap_ret =
        swapchain_builder.set_old_swapchain(renderer.vk.swapchain)
            .set_desired_min_image_count(renderer.MAX_FRAMES_IN_FLIGHT)
            .set_desired_extent(renderer.window.width, renderer.window.height)
            .build();

    if (!swap_ret) {
        // If it failed to create a swapchain, the old swapchain handle is
        // invalid.
        renderer.vk.swapchain = VK_NULL_HANDLE;
    } else {
        // Even though we recycled the previous swapchain, we need to free its
        // resources.
        vkDestroySwapchainKHR(renderer.vk.device, renderer.vk.swapchain,
                              nullptr);
        // Get the new swapchain and place it in our variable
        renderer.vk.swapchain = swap_ret.value();
        // Store views once (vkb creates new each get_image_views call)
        auto views_res = swap_ret.value().get_image_views();
        if (!views_res) {
            LOG_ERROR("Failed to get swapchain image views");
            return;
        }

        renderer.vk.swap_chain_image_views = views_res.value();
    }
}
