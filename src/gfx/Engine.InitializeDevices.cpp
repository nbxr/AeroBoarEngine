#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include "core/Configuration.h"
#include "VkBootstrap.h"
#include <vulkan/vulkan.h>
#include <nlohmann/json.hpp>

// Custom debug messenger callback. Routes all validation layer output through
// our logger (console + aero_boar.log with flush) so messages are retained
// even if the process crashes due to a Vulkan error (DEVICE_LOST, etc.).
//
// We apply light deduplication for repeated identical messages. After a crash
// (e.g. DEVICE_LOST) the main loop can keep calling into Vulkan at high speed;
// without this the log file would be flooded with millions of identical lines
// (as happened with the vkAcquireNextImageKHR VUID after device loss).
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* /*pUserData*/) {

    auto ms = vkb::to_string_message_severity(messageSeverity);
    auto mt = vkb::to_string_message_type(messageType);

    std::string prefix;
    if (pCallbackData->pMessageIdName && pCallbackData->pMessageIdName[0]) {
        prefix = std::string("[") + ms + ": " + mt + "] " + pCallbackData->pMessageIdName + " : ";
    } else {
        prefix = std::string("[") + ms + ": " + mt + "] ";
    }

    std::string full = prefix + (pCallbackData->pMessage ? pCallbackData->pMessage : "");

    // Simple cross-call dedup (not perfect under heavy threading, but good enough
    // for the hot failure loops that produce millions of lines).
    static std::string last_logged;
    static int repeat_count = 0;

    bool is_error_or_warn = (messageSeverity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)) != 0;

    if (full == last_logged) {
        ++repeat_count;
        // Only emit a summary line occasionally for repeats so the file stays usable.
        // For real crashes we still want the first occurrence + a count.
        if (repeat_count % 1000 == 0) {
            std::string summary = full + "  (repeated " + std::to_string(repeat_count) + " times)";
            if (is_error_or_warn) {
                LOG_ERROR(summary);
            } else {
                LOG_INFO(summary);
            }
        }
        return VK_FALSE;
    }

    // Message changed — emit a final repeat summary for the previous one if it repeated a lot.
    if (repeat_count > 0) {
        std::string summary = last_logged + "  (repeated " + std::to_string(repeat_count) + " times total)";
        // We don't know the old severity, so use the current one as approximation; in practice
        // the spammy messages after a crash are the same severity.
        if (is_error_or_warn) {
            LOG_ERROR(summary);
        } else {
            LOG_INFO(summary);
        }
    }

    last_logged = full;
    repeat_count = 0;

    // Push errors and warnings through LOG_ERROR for high visibility in both
    // console and the persistent log file (with flush). Info/verbose go through LOG_INFO.
    if (is_error_or_warn) {
        LOG_ERROR(full);
    } else {
        LOG_INFO(full);
    }

    return VK_FALSE; // continue processing; do not suppress the call to driver
}

bool gfx::Engine::init_vk_instance(vkb::InstanceBuilder &builder) {
    
    // Get system info to check available layers
    auto system_info_ret = vkb::SystemInfo::get_system_info();
    if (!system_info_ret) {
        LOG_ERROR("Failed to get system info: " << system_info_ret.error().message());
        return false;
    }
    
    auto system_info = system_info_ret.value();

    // This layer displays FPS in the window title bar
    if (system_info.is_layer_available("VK_LAYER_LUNARG_monitor")) {
        builder.enable_layer("VK_LAYER_LUNARG_monitor");
    } else {
        std::cout << "Warning: VK_LAYER_LUNARG_monitor not found. FPS counter disabled.\n";
    }

    // === Validation layer + advanced validation features ===
    // .request_validation_layers(true) only loads VK_LAYER_KHRONOS_validation.
    // The really useful things for DEVICE_LOST / texture / bindless bugs are
    // the *features* inside the validation layer:
    //   - Synchronization Validation (catches bad layouts, missing barriers, wrong access masks)
    //   - GPU-Assisted Validation (instruments shaders for OOB, invalid descriptors, bad image sampling)
    //   - Best Practices
    //
    // These are enabled via VkValidationFeaturesEXT (chained at instance creation).
    // We do it here so that a normal Debug run of the exe gets strong validation
    // without requiring the user to run Vulkan Configurator or set env vars every time.
    //
    // Note: GPU-AV has a noticeable perf cost and higher memory use. It is intended
    // for development/debug builds when hunting bugs like the Sponza crash.
    renderer.vk.enable_gpu_assisted_validation =
        core::Configuration::get_instance().find<bool>("gpuAssistedValidation");

    if (renderer.vk.enable_validation_layers) {
        constexpr const char* khronos_validation = "VK_LAYER_KHRONOS_validation";
        if (system_info.is_layer_available(khronos_validation)) {
            builder.request_validation_layers(true);

            // Required to use VkValidationFeaturesEXT
            builder.enable_extension(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

            // Core features for this class of GPU fault:
            builder.add_validation_feature_enable(
                VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);

            if (renderer.vk.enable_gpu_assisted_validation) {
                builder.add_validation_feature_enable(
                    VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
                builder.add_validation_feature_enable(
                    VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
            }

            // Very useful warnings (e.g. too many dedicated allocations, suboptimal image usage, etc.)
            builder.add_validation_feature_enable(
                VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);

            // Uncomment for shader printf debugging during hard investigations:
            // builder.add_validation_feature_enable(VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT);

            LOG_INFO("[Vulkan] Validation layer + Synchronization Validation enabled"
                     << (renderer.vk.enable_gpu_assisted_validation ? " + GPU-Assisted Validation" : " (GPU-AV disabled)")
                     << ".  Validation output will also be written to aero_boar.log (flushed).");
        } else {
            LOG_INFO("Warning: VK_LAYER_KHRONOS_validation not available.");
        }
    }

    // vulkan instance
    // Use our custom debug callback (instead of use_default_debug_messenger)
    // so that validation messages are written to both console *and* the log file
    // (aero_boar.log) with immediate flush. This lets us recover the exact
    // validation errors that precede a crash.
    auto inst_ret =
        builder.set_app_name("AeroBoar")
            .require_api_version(1, 4)
            .set_debug_callback(vulkan_debug_callback)
            .set_debug_messenger_severity(
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            // Note: We deliberately do not request INFO level by default.
            // INFO produces a lot of one-time loader messages at startup (useful)
            // but can also be very chatty at runtime. The dedup logic above plus
            // only ERROR+WARNING keeps the log file practical while still capturing
            // the validation failures you care about for crash diagnosis.
            // If you need more context, temporarily add INFO_BIT_EXT here.
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

    // Multi-draw indirect uses firstInstance → gl_BaseInstance in the VS.
    VkPhysicalDeviceFeatures features10{};
    features10.drawIndirectFirstInstance = VK_TRUE;
    features10.multiDrawIndirect = VK_TRUE;
    selector.set_required_features(features10);

    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.shaderDrawParameters = VK_TRUE; // gl_BaseInstance
    selector.set_required_features_11(features11);

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
    renderer.vk.graphics_family_index = dev.get_queue_index(vkb::QueueType::graphics).value();
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
    renderer.vk.present_family_index = dev.get_queue_index(vkb::QueueType::present).value();
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
            // Quick experiment (Step 2): request one extra image beyond MAX_FRAMES_IN_FLIGHT
            // to give the present engine headroom. With 2 frames + 2 images + vsync we were
            // hitting the "already acquired 1 image" VUID + immediate DEVICE_LOST on submit.
            // For Quest 3 the runtime controls the actual count; desktop can afford a bit more slack.
            .set_desired_min_image_count(renderer.MAX_FRAMES_IN_FLIGHT + 1)
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
    renderer.vk.swap_chain_image_count = static_cast<uint32_t>(renderer.vk.swap_chain_image_views.size());

    LOG_INFO("[Swapchain] Created with " << renderer.vk.swap_chain_image_count
             << " images (requested minImageCount=" << renderer.MAX_FRAMES_IN_FLIGHT + 1 << " for headroom experiment)");

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
    std::vector<AllocatedImage>    new_msaa_images;
    std::vector<AllocatedImage>    new_depth_images;
    std::vector<VkSemaphore>       new_render_finished;
    std::vector<VkFramebuffer>     new_framebuffers;

    bool success = true;

    // --------------------------------------------------------
    // 1. Create new swapchain (using old one as oldSwapchain for efficiency)
    // --------------------------------------------------------
    vkb::SwapchainBuilder swapchain_builder{renderer.vk.device};
    auto swap_ret = swapchain_builder
        .set_old_swapchain(renderer.vk.swapchain)
        // Quick experiment (Step 2): request one extra image beyond MAX_FRAMES_IN_FLIGHT
        // (same rationale as in init_swapchain).
        .set_desired_min_image_count(renderer.MAX_FRAMES_IN_FLIGHT + 1)
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
    // 2. Create new transient MSAA + depth images (one pair per swap image)
    //    so that in-flight frames using different swap images don't overlap
    //    on the same transient attachments (this was causing DEVICE_LOST
    //    on the second frame's submit when 2 frames were in flight).
    // --------------------------------------------------------
    if (success) {
        const uint32_t new_image_count = static_cast<uint32_t>(new_swapchain_views.size());
        new_msaa_images.resize(new_image_count);
        new_depth_images.resize(new_image_count);
        for (uint32_t i = 0; i < new_image_count; ++i) {
            if (!create_msaa_color_image(renderer.vk.swap_chain_extent, new_msaa_images[i])) {
                LOG_ERROR("Failed to create new MSAA color image during recreation");
                success = false;
                break;
            }
            if (!create_depth_image(renderer.vk.swap_chain_extent, new_depth_images[i])) {
                LOG_ERROR("Failed to create new depth image during recreation");
                success = false;
                break;
            }
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
    // 5. Create new framebuffers using the new images (each gets its own
    //    transient MSAA + depth so concurrent frames don't share them).
    // --------------------------------------------------------
    if (success) {
        const uint32_t new_image_count = static_cast<uint32_t>(new_swapchain_views.size());
        new_framebuffers.resize(new_image_count);

        for (uint32_t i = 0; i < new_image_count; ++i) {
            VkImageView attachments[3] = {
                new_msaa_images[i].view,
                new_swapchain_views[i],
                new_depth_images[i].view
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

        // Destroy old per-swap-image transient MSAA and depth images
        for (auto &img : renderer.main_pass.msaa_color_images) {
            if (img.view != VK_NULL_HANDLE) {
                vkDestroyImageView(renderer.vk.device, img.view, nullptr);
            }
            if (img.handle != VK_NULL_HANDLE) {
                vmaDestroyImage(renderer.allocator, img.handle, img.allocation);
            }
        }
        renderer.main_pass.msaa_color_images.clear();

        for (auto &img : renderer.main_pass.depth_images) {
            if (img.view != VK_NULL_HANDLE) {
                vkDestroyImageView(renderer.vk.device, img.view, nullptr);
            }
            if (img.handle != VK_NULL_HANDLE) {
                vmaDestroyImage(renderer.allocator, img.handle, img.allocation);
            }
        }
        renderer.main_pass.depth_images.clear();

        // Adopt new resources
        renderer.vk.swapchain = new_swapchain;
        renderer.vk.swap_chain_image_views = std::move(new_swapchain_views);
        renderer.vk.swap_chain_image_count = static_cast<uint32_t>(renderer.vk.swap_chain_image_views.size());
        renderer.main_pass.msaa_color_images = std::move(new_msaa_images);
        renderer.main_pass.depth_images = std::move(new_depth_images);
        renderer.vk.render_finished_semaphores = std::move(new_render_finished);
        renderer.main_pass.framebuffers = std::move(new_framebuffers);

        // Update window size tracking
        renderer.window.width = renderer.vk.swap_chain_extent.width;
        renderer.window.height = renderer.vk.swap_chain_extent.height;

        // Restart frame index after swapchain recreation. The per-frame fences
        // and image_available semaphores were not recreated here (they are
        // sized to MAX_FRAMES_IN_FLIGHT, not tied to a particular swapchain),
        // but starting back at 0 gives a clean sequence for the next render()
        // calls. The first post-recreate wait should still work because we did
        // a full vkDeviceWaitIdle above.
        renderer.current_frame = 0;

        LOG_INFO("[Swapchain] Recreated with " << renderer.vk.swap_chain_image_count
                 << " images (requested minImageCount=" << renderer.MAX_FRAMES_IN_FLIGHT + 1 << " for headroom experiment)");

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

        // Cleanup new per-image transient MSAA and depth images
        for (auto &img : new_msaa_images) {
            if (img.view != VK_NULL_HANDLE) {
                vkDestroyImageView(renderer.vk.device, img.view, nullptr);
            }
            if (img.handle != VK_NULL_HANDLE) {
                vmaDestroyImage(renderer.allocator, img.handle, img.allocation);
            }
        }
        for (auto &img : new_depth_images) {
            if (img.view != VK_NULL_HANDLE) {
                vkDestroyImageView(renderer.vk.device, img.view, nullptr);
            }
            if (img.handle != VK_NULL_HANDLE) {
                vmaDestroyImage(renderer.allocator, img.handle, img.allocation);
            }
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
