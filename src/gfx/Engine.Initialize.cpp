#define VMA_IMPLEMENTATION
#include "gfx/Engine.h"
#include "gfx/AllocatedBuffer.h"
#include "gfx/AllocatedImage.h"
#include "gfx/BufferUtils.h"
#include "scene/GameObject.h"
#include "scene/RenderMesh.h"
#include "gfx/Renderer.h"
#include "gfx/Light.h"
#include "core/Configuration.h"
#include "core/Log.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"
#include <filesystem>
#include <string>

bool gfx::Engine::initialize() {
    if (init_vulkan()) {
        return true;
    } else
        return false;
}

bool gfx::Engine::init_vulkan() {
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
    select_sample_counts(phys);

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

    // Depth-only prepass (same-frame Hi-Z source) before main targets.
    if (!init_depth_prepass())
        return false;

    // Initialize MSAA and depth images
    if (!init_msaa_color_image())
        return false;

    if (!init_depth_image())
        return false;

    // Initialize descriptor pool and set layout for bindless rendering
    if (!init_descriptor_pool())
        return false;
    if (!init_descriptor_set_layout())
        return false;

    if (!init_bindless_descriptor_set())
        return false;

    // Initialize pipeline layout
    if (!init_pipeline_layout())
        return false;

    // Initialize graphics pipeline
    if (!init_graphics_pipeline())
        return false;

    if (!init_depth_prepass_pipeline())
        return false;

    // Physics / editor wireframe overlay (after main_pass render pass exists).
    if (!renderer.debug_lines.create(renderer.vk.device, renderer.allocator,
                                     renderer.main_pass.render_pass,
                                     renderer.vk.msaa_color)) {
        LOG_ERROR("[Init] DebugLinePass failed (physics debug draw disabled)");
    }

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

bool gfx::Engine::init_vma() {
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

bool gfx::Engine::init_surface() {
    // surface
    if (glfwCreateWindowSurface(renderer.vk.instance,
                                renderer.window.glfw_handle, nullptr,
                                &renderer.vk.surface) != VK_SUCCESS) {
        LOG_ERROR("Failed to create Vulkan surface");
        return false;
    }
    return true;
}

bool gfx::Engine::init_resource_managers() {
    // Initialize managers. The initial capacity is passed; binding to specific
    // descriptor sets happens later via explicit bind_descriptor(target) calls
    // (replicated to all per-frame sets at load time in InitializeScene).
    if (!renderer.scene_manager.initialize(
            renderer.vk.device.device, renderer.allocator, 100))
        return false;

    if (!renderer.material_manager.initialize(
            renderer.vk.device.device, renderer.allocator, 100))
        return false;

    if (!renderer.mesh_manager.initialize(
            renderer.vk.device.device, renderer.allocator, 100))
        return false;

    if (!renderer.texture_manager.initialize(
            renderer.vk.device.device, renderer.allocator,
            renderer.vk.transfer_queue, renderer.vk.graphics_queue,
            renderer.vk.graphics_family_index,
            renderer.vk.transfer_family_index))
        return false;

    // FrameConstants UBO (binding 0) + lights SSBO (binding 6), double-buffered.
    VkDeviceSize constantsSize = sizeof(gfx::FrameConstants);
    VkBufferUsageFlags uboUsage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VkDeviceSize lightsSize =
        static_cast<VkDeviceSize>(gfx::MAX_LIGHTS) * sizeof(gfx::GpuLight);

    bool c0 = gfx::BufferUtils::initialize_buffer(
        renderer.vk.device.device, renderer.allocator, constantsSize,
        renderer.frame_constants_buffer[0], uboUsage);
    bool c1 = gfx::BufferUtils::initialize_buffer(
        renderer.vk.device.device, renderer.allocator, constantsSize,
        renderer.frame_constants_buffer[1], uboUsage);
    bool l0 = gfx::BufferUtils::initialize_buffer(
        renderer.vk.device.device, renderer.allocator, lightsSize,
        renderer.frame_lights_buffer[0]);
    bool l1 = gfx::BufferUtils::initialize_buffer(
        renderer.vk.device.device, renderer.allocator, lightsSize,
        renderer.frame_lights_buffer[1]);

    if (!c0 || !c1 || !l0 || !l1) {
        LOG_ERROR("Failed to create per-frame lighting buffers (constants UBO + lights SSBO)");
        return false;
    }

    // Optional HDR equirect for IBL (configuration.json "environmentHdr").
    // Path is absolute, or relative to the active home path / cwd.
    {
        const auto& cfg = core::Configuration::get_instance();
        if (cfg.is_loaded()) {
            const auto& root = core::Configuration::get_root();
            if (root.contains("environmentHdr") && root["environmentHdr"].is_string()) {
                std::string hdr = root["environmentHdr"].get<std::string>();
                if (!hdr.empty() && !std::filesystem::path(hdr).is_absolute()) {
                    std::string home_path;
                    std::string active_system =
                        root.value("activeSystem", std::string{});
                    if (root.contains("home") && root["home"].is_array()) {
                        for (const auto& entry : root["home"]) {
                            if (entry.contains("system") && entry.contains("path") &&
                                entry["system"].get<std::string>() == active_system) {
                                home_path = entry["path"].get<std::string>();
                                break;
                            }
                        }
                    }
                    if (!home_path.empty()) {
                        hdr = (std::filesystem::path(home_path) / hdr)
                                  .make_preferred()
                                  .string();
                    }
                }
                renderer.ibl.equirect_hdr_path = hdr;
                if (!hdr.empty())
                    LOG_INFO("[IBL] environmentHdr = " << hdr);
            }
        }
    }

    // IBL (procedural or HDR equirect). Non-fatal if bake fails.
    if (!renderer.ibl.initialize(renderer.vk.device.device, renderer.allocator,
                                 renderer.vk.graphics_queue,
                                 renderer.vk.graphics_family_index)) {
        LOG_ERROR("[IBL] initialize failed — continuing without specular IBL");
    }

    if (!renderer.gpu_culling.initialize(renderer.vk.device.device, renderer.allocator)) {
        LOG_ERROR("[GpuCulling] initialize failed — will fall back if scene has no cull data");
    }

    if (!renderer.hzb.initialize(renderer.vk.device.device, renderer.allocator)) {
        LOG_ERROR("[Hzb] initialize failed — occlusion culling disabled");
    } else if (!renderer.hzb.resize(renderer.vk.device.device, renderer.allocator,
                                    renderer.vk.swap_chain_extent)) {
        LOG_ERROR("[Hzb] resize failed — occlusion culling disabled");
    } else {
        wire_hzb_descriptors();
    }

    configure_occlusion_cull();

    return true;
}

void gfx::Engine::configure_occlusion_cull() {
    // Default off: frustum always; extra depth prepass is optional on desktop.
#ifdef AERO_TARGET_ADRENO
    occlusion_cull_enabled_ = false;
    LOG_INFO("[Cull] occlusionCull forced OFF (AERO_TARGET_ADRENO / GMEM)");
    return;
#else
    const auto& cfg = core::Configuration::get_instance();
    // find<bool> accepts a root bool or { "enabled": bool }. Missing → false.
    occlusion_cull_enabled_ = cfg.is_loaded() && cfg.find<bool>("occlusionCull");
    LOG_INFO("[Cull] occlusionCull="
             << (occlusion_cull_enabled_ ? "ON (same-frame Hi-Z)" : "OFF (frustum only)"));
#endif
}

void gfx::Engine::wire_hzb_descriptors() {
    if (!renderer.hzb.is_ready())
        return;

    const uint32_t n = Renderer::MAX_FRAMES_IN_FLIGHT;
    for (uint32_t f = 0; f < n; ++f) {
        if (f < renderer.depth_prepass.depth_images.size() &&
            renderer.depth_prepass.depth_images[f].view != VK_NULL_HANDLE) {
            renderer.hzb.bind_depth_source(f, renderer.depth_prepass.depth_images[f].view);
        }
        if (renderer.gpu_culling.is_ready()) {
            renderer.gpu_culling.bind_hzb(f, renderer.hzb.full_view(f),
                                          renderer.hzb.sampler());
        }
    }

    // Pyramid images start UNDEFINED; cull descriptors expect GENERAL. Transition
    // once after resize so the first frame never references UNDEFINED HZB.
    if (renderer.hzb.needs_layout_init() &&
        renderer.vk.generic_command_pool != VK_NULL_HANDLE &&
        renderer.vk.graphics_queue != VK_NULL_HANDLE) {
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = renderer.vk.generic_command_pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(renderer.vk.device, &alloc, &cmd) == VK_SUCCESS) {
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(cmd, &begin) == VK_SUCCESS) {
                renderer.hzb.record_init_layouts(cmd);
                vkEndCommandBuffer(cmd);

                VkSubmitInfo submit{};
                submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &cmd;
                vkQueueSubmit(renderer.vk.graphics_queue, 1, &submit, VK_NULL_HANDLE);
                vkQueueWaitIdle(renderer.vk.graphics_queue);
            }
            vkFreeCommandBuffers(renderer.vk.device, renderer.vk.generic_command_pool, 1,
                                 &cmd);
        }
    }
}
