#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include "VkBootstrap.h"

bool gfx::Engine::init_vk_instance(vkb::InstanceBuilder &builder) {
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

void gfx::Engine::add_features(vkb::PhysicalDeviceSelector &selector) {
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
    features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.bufferDeviceAddress = VK_TRUE;
    selector.set_required_features_12(features12);
}

std::pair<bool, vkb::PhysicalDevice> gfx::Engine::init_physical_device() {
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
gfx::Engine::init_logical_device(vkb::PhysicalDevice &phys) {
    vkb::DeviceBuilder device_builder{phys};
    auto dev_ret = device_builder.build();
    if (!dev_ret) {
        LOG_ERROR("Failed to find suitable logical device");
        return {false, vkb::Device{}};
    }
    renderer.vk.device = dev_ret.value();
    return {true, dev_ret.value()};
}

void gfx::Engine::select_depth_format(vkb::PhysicalDevice &phys) {
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

void gfx::Engine::select_sample_counts(vkb::PhysicalDevice &phys) {
    // Prefer 4x MSAA if supported, fall back to 2x, then 1x (no MSAA)
    VkSampleCountFlags counts = phys.properties.limits.framebufferColorSampleCounts &
                                phys.properties.limits.framebufferDepthSampleCounts;

    if (counts & VK_SAMPLE_COUNT_4_BIT) {
        renderer.vk.msaa_color = VK_SAMPLE_COUNT_4_BIT;
        renderer.vk.msaa_depth = VK_SAMPLE_COUNT_4_BIT;
    } else if (counts & VK_SAMPLE_COUNT_2_BIT) {
        renderer.vk.msaa_color = VK_SAMPLE_COUNT_2_BIT;
        renderer.vk.msaa_depth = VK_SAMPLE_COUNT_2_BIT;
    } else {
        renderer.vk.msaa_color = VK_SAMPLE_COUNT_1_BIT;
        renderer.vk.msaa_depth = VK_SAMPLE_COUNT_1_BIT;
    }
}

bool gfx::Engine::init_graphics_queue(vkb::Device &dev) {
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

bool gfx::Engine::init_present_queue(vkb::Device &dev) {
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

bool gfx::Engine::init_transfer_queue(vkb::Device &dev) {
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

bool gfx::Engine::init_swapchain(vkb::Device &dev) {
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

void gfx::Engine::recreate_swapchain() {
    vkDeviceWaitIdle(renderer.vk.device);

    // ============================================================
    // TWO-PHASE RECREATION (Error Recovery Pattern)
    // Phase 1: Create ALL new resources while old ones are still valid.
    // Phase 2: On full success -> destroy old + adopt new.
    //          On any failure  -> clean up what we created and keep old.
    // ============================================================

    // --- Local variables for new resources ---
    VkSwapchainKHR                 new_swapchain = VK_NULL_HANDLE;
    std::vector<VkImageView>       new_swapchain_views;
    AllocatedImage                 new_msaa;
    AllocatedImage                 new_depth;
    std::vector<VkSemaphore>       new_render_finished;
    std::vector<VkFramebuffer>     new_framebuffers;

    bool success = true;

    // --------------------------------------------------------
    // 1. Create new swapchain (using old one as oldSwapchain for efficiency)
    // --------------------------------------------------------
    vkb::SwapchainBuilder swapchain_builder{renderer.vk.device};
    auto swap_ret = swapchain_builder
        .set_old_swapchain(renderer.vk.swapchain)
        .set_desired_min_image_count(renderer.MAX_FRAMES_IN_FLIGHT)
        .set_desired_extent(renderer.window.width, renderer.window.height)
        .build();

    if (!swap_ret) {
        LOG_ERROR("Failed to create new swapchain during recreation");
        success = false;
    } else {
        new_swapchain = swap_ret.value().swapchain;
        renderer.vk.swap_chain_image_format = swap_ret.value().image_format;
        renderer.vk.swap_chain_extent = swap_ret.value().extent;

        auto views_res = swap_ret.value().get_image_views();
        if (!views_res) {
            LOG_ERROR("Failed to get image views for new swapchain");
            success = false;
        } else {
            new_swapchain_views = std::move(views_res.value());
        }
    }

    // --------------------------------------------------------
    // 2. Create new transient MSAA color image at new size
    // --------------------------------------------------------
    if (success) {
        if (!create_msaa_color_image(renderer.vk.swap_chain_extent, new_msaa)) {
            LOG_ERROR("Failed to create new MSAA color image during recreation");
            success = false;
        }
    }

    // --------------------------------------------------------
    // 3. Create new transient depth image at new size
    // --------------------------------------------------------
    if (success) {
        if (!create_depth_image(renderer.vk.swap_chain_extent, new_depth)) {
            LOG_ERROR("Failed to create new depth image during recreation");
            success = false;
        }
    }

    // --------------------------------------------------------
    // 4. Create new render-finished semaphores (sized to new image count)
    // --------------------------------------------------------
    if (success) {
        const uint32_t new_image_count = static_cast<uint32_t>(new_swapchain_views.size());
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        new_render_finished.resize(new_image_count);
        for (uint32_t i = 0; i < new_image_count; ++i) {
            if (vkCreateSemaphore(renderer.vk.device, &semaphore_info, nullptr,
                                  &new_render_finished[i]) != VK_SUCCESS) {
                LOG_ERROR("Failed to create new render finished semaphore");
                success = false;
                break;
            }
        }
    }

    // --------------------------------------------------------
    // 5. Create new framebuffers using the new images
    // --------------------------------------------------------
    if (success) {
        const uint32_t new_image_count = static_cast<uint32_t>(new_swapchain_views.size());
        new_framebuffers.resize(new_image_count);

        for (uint32_t i = 0; i < new_image_count; ++i) {
            VkImageView attachments[3] = {
                new_msaa.view,
                new_swapchain_views[i],
                new_depth.view
            };

            VkFramebufferCreateInfo fb_info{};
            fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb_info.renderPass = renderer.main_pass.render_pass;
            fb_info.attachmentCount = 3;
            fb_info.pAttachments = attachments;
            fb_info.width = renderer.vk.swap_chain_extent.width;
            fb_info.height = renderer.vk.swap_chain_extent.height;
            fb_info.layers = 1;

            if (vkCreateFramebuffer(renderer.vk.device, &fb_info, nullptr,
                                  &new_framebuffers[i]) != VK_SUCCESS) {
                LOG_ERROR("Failed to create new framebuffer during recreation");
                success = false;
                break;
            }
        }
    }

    // ============================================================
    // PHASE 2: Commit or Rollback
    // ============================================================
    if (success) {
        // --- COMMIT: Everything succeeded. Destroy old resources and adopt new ones ---

        // Destroy old framebuffers
        for (auto fb : renderer.main_pass.framebuffers) {
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(renderer.vk.device, fb, nullptr);
        }
        renderer.main_pass.framebuffers.clear();

        // Destroy old swapchain image views
        for (auto view : renderer.vk.swap_chain_image_views) {
            if (view != VK_NULL_HANDLE) vkDestroyImageView(renderer.vk.device, view, nullptr);
        }
        renderer.vk.swap_chain_image_views.clear();

        // Destroy old swapchain
        if (renderer.vk.swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(renderer.vk.device, renderer.vk.swapchain, nullptr);
        }

        // Destroy old semaphores
        for (auto sem : renderer.vk.render_finished_semaphores) {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(renderer.vk.device, sem, nullptr);
        }

        // Destroy old MSAA and depth images
        if (renderer.main_pass.msaa_color_image.view != VK_NULL_HANDLE) {
            vkDestroyImageView(renderer.vk.device, renderer.main_pass.msaa_color_image.view, nullptr);
        }
        if (renderer.main_pass.msaa_color_image.handle != VK_NULL_HANDLE) {
            vmaDestroyImage(renderer.allocator, renderer.main_pass.msaa_color_image.handle,
                            renderer.main_pass.msaa_color_image.allocation);
        }

        if (renderer.main_pass.depth_image.view != VK_NULL_HANDLE) {
            vkDestroyImageView(renderer.vk.device, renderer.main_pass.depth_image.view, nullptr);
        }
        if (renderer.main_pass.depth_image.handle != VK_NULL_HANDLE) {
            vmaDestroyImage(renderer.allocator, renderer.main_pass.depth_image.handle,
                            renderer.main_pass.depth_image.allocation);
        }

        // Adopt new resources
        renderer.vk.swapchain = new_swapchain;
        renderer.vk.swap_chain_image_views = std::move(new_swapchain_views);
        renderer.main_pass.msaa_color_image = new_msaa;
        renderer.main_pass.depth_image = new_depth;
        renderer.vk.render_finished_semaphores = std::move(new_render_finished);
        renderer.main_pass.framebuffers = std::move(new_framebuffers);

        // Update window size tracking
        renderer.window.width = renderer.vk.swap_chain_extent.width;
        renderer.window.height = renderer.vk.swap_chain_extent.height;

    } else {
        // --- ROLLBACK: Something failed. Clean up everything we created and keep old resources ---
        LOG_ERROR("Swapchain recreation failed. Keeping previous resources.");

        // Cleanup any new framebuffers we managed to create
        for (auto fb : new_framebuffers) {
            if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(renderer.vk.device, fb, nullptr);
        }

        // Cleanup new semaphores
        for (auto sem : new_render_finished) {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(renderer.vk.device, sem, nullptr);
        }

        // Cleanup new depth image
        if (new_depth.view != VK_NULL_HANDLE) {
            vkDestroyImageView(renderer.vk.device, new_depth.view, nullptr);
        }
        if (new_depth.handle != VK_NULL_HANDLE) {
            vmaDestroyImage(renderer.allocator, new_depth.handle, new_depth.allocation);
        }

        // Cleanup new MSAA image
        if (new_msaa.view != VK_NULL_HANDLE) {
            vkDestroyImageView(renderer.vk.device, new_msaa.view, nullptr);
        }
        if (new_msaa.handle != VK_NULL_HANDLE) {
            vmaDestroyImage(renderer.allocator, new_msaa.handle, new_msaa.allocation);
        }

        // Cleanup new swapchain views
        for (auto view : new_swapchain_views) {
            if (view != VK_NULL_HANDLE) vkDestroyImageView(renderer.vk.device, view, nullptr);
        }

        // Cleanup new swapchain itself
        if (new_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(renderer.vk.device, new_swapchain, nullptr);
        }

        // Old resources remain untouched and valid. The next frame or resize attempt can retry.
    }
}
