#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include "gfx/BufferUtils.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void gfx::Engine::destroy() {
    vkDeviceWaitIdle(renderer.vk.device);

    if (physics.is_initialized())
        physics.shutdown();

    destroy_buffers();
    destroy_images();
    destroy_command_buffers();
    destroy_swapchain();
    destroy_framebuffers();
    destroy_sync_primitives();
    destroy_descriptor_pool();
    destroy_pipelines();
    destroy_render_targets();
    destroy_resource_managers();
    destroy_vma();
    destroy_devices();
}

void gfx::Engine::destroy_buffers() {
    renderer.scene_manager.skins().destroy(renderer.vk.device, renderer.allocator);
    renderer.debug_lines.destroy(renderer.vk.device, renderer.allocator);
    renderer.hzb.destroy(renderer.vk.device, renderer.allocator);
    renderer.gpu_culling.destroy(renderer.vk.device, renderer.allocator);
    renderer.ibl.destroy(renderer.vk.device, renderer.allocator);
    gfx::BufferUtils::destroy_buffer(renderer.vk.device, renderer.allocator, renderer.frame_constants_buffer[0]);
    gfx::BufferUtils::destroy_buffer(renderer.vk.device, renderer.allocator, renderer.frame_constants_buffer[1]);
    gfx::BufferUtils::destroy_buffer(renderer.vk.device, renderer.allocator, renderer.frame_lights_buffer[0]);
    gfx::BufferUtils::destroy_buffer(renderer.vk.device, renderer.allocator, renderer.frame_lights_buffer[1]);
}

void gfx::Engine::destroy_images() {

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

    for (auto &img : renderer.main_pass.resolved_depth_images) {
        if (img.view != VK_NULL_HANDLE) {
            vkDestroyImageView(renderer.vk.device, img.view, nullptr);
        }
        if (img.handle != VK_NULL_HANDLE) {
            vmaDestroyImage(renderer.allocator, img.handle, img.allocation);
        }
    }
    renderer.main_pass.resolved_depth_images.clear();

    for (auto &img : renderer.depth_prepass.depth_images) {
        if (img.view != VK_NULL_HANDLE) {
            vkDestroyImageView(renderer.vk.device, img.view, nullptr);
        }
        if (img.handle != VK_NULL_HANDLE) {
            vmaDestroyImage(renderer.allocator, img.handle, img.allocation);
        }
    }
    renderer.depth_prepass.depth_images.clear();
}

void gfx::Engine::destroy_command_buffers() {

    vkDestroyCommandPool(renderer.vk.device, renderer.vk.generic_command_pool,
                         nullptr);
}

void gfx::Engine::destroy_swapchain() {
    for (auto& image_view : renderer.vk.swap_chain_image_views) {
        vkDestroyImageView(renderer.vk.device, image_view, nullptr);
    }
    vkDestroySwapchainKHR(renderer.vk.device, renderer.vk.swapchain,
                          nullptr);
}

void gfx::Engine::destroy_resource_managers() {
    renderer.material_manager.shutdown();
    renderer.mesh_manager.shutdown();
    renderer.texture_manager.shutdown();
    renderer.scene_manager.shutdown();
}

void gfx::Engine::destroy_sync_primitives() {
    // image_available_semaphores (per frame in flight)
    for (auto sem : renderer.vk.image_available_semaphores) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(renderer.vk.device, sem, nullptr);
    }
    renderer.vk.image_available_semaphores.clear();

    // render_finished_semaphores (per swapchain image)
    for (auto sem : renderer.vk.render_finished_semaphores) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(renderer.vk.device, sem, nullptr);
    }
    renderer.vk.render_finished_semaphores.clear();

    // per-frame fences
    for (auto &frame : renderer.frames) {
        if (frame.in_flight_fence != VK_NULL_HANDLE) {
            vkDestroyFence(renderer.vk.device, frame.in_flight_fence, nullptr);
        }
    }
}

void gfx::Engine::destroy_descriptor_pool() {
    vkDestroyDescriptorSetLayout(renderer.vk.device,
                                 renderer.vk.descriptor_set_layout, nullptr);
    vkDestroyDescriptorPool(renderer.vk.device, renderer.vk.descriptor_pool,
                            nullptr);
}

void gfx::Engine::destroy_pipelines() {
    if (renderer.vk.depth_prepass_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(renderer.vk.device, renderer.vk.depth_prepass_pipeline, nullptr);
        renderer.vk.depth_prepass_pipeline = VK_NULL_HANDLE;
    }
    if (renderer.vk.transparent_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(renderer.vk.device, renderer.vk.transparent_pipeline, nullptr);
        renderer.vk.transparent_pipeline = VK_NULL_HANDLE;
    }
    if (renderer.vk.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(renderer.vk.device, renderer.vk.pipeline, nullptr);
        renderer.vk.pipeline = VK_NULL_HANDLE;
    }
    if (renderer.vk.pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(renderer.vk.device, renderer.vk.pipeline_layout, nullptr);
        renderer.vk.pipeline_layout = VK_NULL_HANDLE;
    }
}

void gfx::Engine::destroy_render_targets() {
    if (renderer.depth_prepass.render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(renderer.vk.device, renderer.depth_prepass.render_pass,
                            nullptr);
        renderer.depth_prepass.render_pass = VK_NULL_HANDLE;
    }
    if (renderer.main_pass.render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(renderer.vk.device, renderer.main_pass.render_pass,
                            nullptr);
        renderer.main_pass.render_pass = VK_NULL_HANDLE;
    }
}

void gfx::Engine::destroy_framebuffers() {
    for (auto &framebuffer : renderer.depth_prepass.framebuffers) {
        if (framebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(renderer.vk.device, framebuffer, nullptr);
    }
    renderer.depth_prepass.framebuffers.clear();

    for (auto &framebuffer : renderer.main_pass.framebuffers) {
        if (framebuffer != VK_NULL_HANDLE)
            vkDestroyFramebuffer(renderer.vk.device, framebuffer, nullptr);
    }
    renderer.main_pass.framebuffers.clear();
}

void gfx::Engine::destroy_vma() { vmaDestroyAllocator(renderer.allocator); }

void gfx::Engine::destroy_devices() {
    vkb::destroy_debug_utils_messenger(renderer.vk.instance.instance,
                                       renderer.vk.instance.debug_messenger,
                                       nullptr);

    vkDestroySurfaceKHR(renderer.vk.instance, renderer.vk.surface, nullptr);
    vkDestroyDevice(renderer.vk.device, nullptr);
    vkDestroyInstance(renderer.vk.instance, nullptr);
}
