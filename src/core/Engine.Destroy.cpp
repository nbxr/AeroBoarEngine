#include "Engine.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void core::Engine::destroy() {
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

void core::Engine::destroy_buffers() {}

void core::Engine::destroy_images() {

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

void core::Engine::destroy_command_buffers() {

    vkDestroyCommandPool(renderer.vk.device, renderer.vk.generic_command_pool,
                         nullptr);
}

void core::Engine::destroy_swapchain() {
    for (auto image_view : renderer.vk.swapchain.get_image_views().value()) {
        vkDestroyImageView(renderer.vk.device, image_view, nullptr);
    }
    vkDestroySwapchainKHR(renderer.vk.device, renderer.vk.swapchain.swapchain,
                          nullptr);
}

void core::Engine::destroy_resource_managers() {
    renderer.material_manager.shutdown();
    renderer.mesh_manager.shutdown();
    renderer.texture_manager.shutdown();
    renderer.scene_manager.shutdown();
}

void core::Engine::destroy_sync_primitives() {
    for (auto &frame : renderer.frames) {
        vkDestroySemaphore(renderer.vk.device, frame.image_available_semaphore,
                           nullptr);
        vkDestroySemaphore(renderer.vk.device, frame.render_finished_semaphore,
                           nullptr);
        vkDestroyFence(renderer.vk.device, frame.in_flight_fence, nullptr);
    }
}

void core::Engine::destroy_descriptor_pool() {
    vkDestroyDescriptorSetLayout(renderer.vk.device,
                                 renderer.vk.descriptor_set_layout, nullptr);
    vkDestroyDescriptorPool(renderer.vk.device, renderer.vk.descriptor_pool,
                            nullptr);
}

void core::Engine::destroy_pipelines() {
    vkDestroyPipeline(renderer.vk.device, renderer.vk.pipeline, nullptr);
    vkDestroyPipelineLayout(renderer.vk.device, renderer.vk.pipeline_layout,
                            nullptr);
}

void core::Engine::destroy_render_targets() {
    vkDestroyRenderPass(renderer.vk.device, renderer.main_pass.render_pass,
                        nullptr);
}

void core::Engine::destroy_framebuffers() {
    for (auto &framebuffer : renderer.main_pass.framebuffers) {
        vkDestroyFramebuffer(renderer.vk.device, framebuffer, nullptr);
    }
    renderer.main_pass.framebuffers.clear();
}

void core::Engine::destroy_vma() { vmaDestroyAllocator(renderer.allocator); }

void core::Engine::destroy_devices() {
    vkb::destroy_debug_utils_messenger(renderer.vk.instance.instance,
                                       renderer.vk.instance.debug_messenger,
                                       nullptr);

    vkDestroySurfaceKHR(renderer.vk.instance, renderer.vk.surface, nullptr);
    vkDestroyDevice(renderer.vk.device, nullptr);
    vkDestroyInstance(renderer.vk.instance, nullptr);
}
