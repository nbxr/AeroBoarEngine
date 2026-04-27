#include "Engine.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void core::Engine::destroy() {
    vkDeviceWaitIdle(renderer.vk.device);

    destroy_buffers();
    destroy_images();
    destroy_command_buffers();

    destroy_sync_primitives();
    destroy_descriptor_pool();
    destroy_pipelines();
    destroy_render_targets();
    destroy_vma();
    destroy_devices();
}

void core::Engine::destroy_buffers() {
    // for (auto &buffer : renderer.buffers) {
    //     vkDestroyBuffer(renderer.vk.device, buffer.buffer, nullptr);
    //     vmaFreeMemory(renderer.allocator, buffer.allocation);
    // }
    // renderer.buffers.clear();
}

void core::Engine::destroy_images() {
    // for (auto &image : renderer.images) {
    //     vkDestroyImageView(renderer.vk.device, image.view, nullptr);
    //     vkDestroyImage(renderer.vk.device, image.image, nullptr);
    //     vmaFreeMemory(renderer.allocator, image.allocation);
    // }
    // renderer.images.clear();
}

void core::Engine::destroy_command_buffers() {
    // vkFreeCommandBuffers(renderer.vk.device, renderer.vk.command_pool,
    //                      renderer.vk.command_buffers.size(),
    //                      renderer.vk.command_buffers.data());
    // renderer.vk.command_buffers.clear();
}

void core::Engine::destroy_sync_primitives() {
    for (auto &frame : renderer.frames) {
        vkDestroySemaphore(renderer.vk.device, frame.image_available_semaphore,nullptr);
        vkDestroySemaphore(renderer.vk.device, frame.render_finished_semaphore,nullptr);
        vkDestroyFence(renderer.vk.device, frame.in_flight_fence, nullptr);
    }
}

void core::Engine::destroy_descriptor_pool() {
    vkDestroyDescriptorSetLayout(renderer.vk.device,
                                 renderer.vk.descriptor_set_layout, nullptr);
    vkDestroyDescriptorPool(renderer.vk.device, renderer.vk.descriptor_pool, nullptr);
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

void core::Engine::destroy_vma() {
    vmaDestroyAllocator(renderer.allocator);
}

void core::Engine::destroy_devices() {
    vkDestroyDevice(renderer.vk.device, nullptr);
    vkDestroyInstance(renderer.vk.instance, nullptr);
}
