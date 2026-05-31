#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include "gfx/BufferUtils.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void gfx::Engine::destroy() {
    vkDeviceWaitIdle(renderer.vk.device);

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
    // Phase 2 lighting globals UBOs
    gfx::BufferUtils::destroy_buffer(renderer.vk.device, renderer.allocator, renderer.frame_globals_buffer[0]);
    gfx::BufferUtils::destroy_buffer(renderer.vk.device, renderer.allocator, renderer.frame_globals_buffer[1]);
}

void gfx::Engine::destroy_images() {

    vkDestroyImageView(renderer.vk.device,
                       renderer.main_pass.msaa_color_image.view, nullptr);

    vkDestroyImageView(renderer.vk.device, renderer.main_pass.depth_image.view,
                       nullptr);
                       
    vmaDestroyImage(renderer.allocator,
                    renderer.main_pass.msaa_color_image.handle,
                    renderer.main_pass.msaa_color_image.allocation);

    vmaDestroyImage(renderer.allocator, renderer.main_pass.depth_image.handle,
                    renderer.main_pass.depth_image.allocation);


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
    vkDestroyPipeline(renderer.vk.device, renderer.vk.pipeline, nullptr);
    vkDestroyPipelineLayout(renderer.vk.device, renderer.vk.pipeline_layout,
                            nullptr);
}

void gfx::Engine::destroy_render_targets() {
    vkDestroyRenderPass(renderer.vk.device, renderer.main_pass.render_pass,
                        nullptr);
}

void gfx::Engine::destroy_framebuffers() {
    for (auto &framebuffer : renderer.main_pass.framebuffers) {
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
