#include "Engine.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void core::Engine::destroy(core::Renderer &renderer) {
    vkDeviceWaitIdle(renderer.vk.device);

    destroy_buffers(renderer);
    destroy_images(renderer);
    destroy_command_buffers(renderer);

    destroy_sync_primitives(renderer);
    destroy_descriptor_pool(renderer);
    destroy_pipelines(renderer);
    destroy_render_targets(renderer);
    destroy_vma(renderer);
    destroy_devices(renderer);
}

void core::Engine::destroy_buffers(core::Renderer &renderer) {
    // for (auto &buffer : renderer.buffers) {
    //     vkDestroyBuffer(renderer.vk.device, buffer.buffer, nullptr);
    //     vmaFreeMemory(renderer.allocator, buffer.allocation);
    // }
    // renderer.buffers.clear();
}

void core::Engine::destroy_images(core::Renderer &renderer) {
    // for (auto &image : renderer.images) {
    //     vkDestroyImageView(renderer.vk.device, image.view, nullptr);
    //     vkDestroyImage(renderer.vk.device, image.image, nullptr);
    //     vmaFreeMemory(renderer.allocator, image.allocation);
    // }
    // renderer.images.clear();
}

void core::Engine::destroy_command_buffers(core::Renderer &renderer) {
    // vkFreeCommandBuffers(renderer.vk.device, renderer.vk.command_pool,
    //                      renderer.vk.command_buffers.size(),
    //                      renderer.vk.command_buffers.data());
    // renderer.vk.command_buffers.clear();
}

void core::Engine::destroy_sync_primitives(core::Renderer &renderer) {
    for (auto &semaphore : renderer.vk.render_finished_semaphores) {
        vkDestroySemaphore(renderer.vk.device, semaphore, nullptr);
    }
    renderer.vk.render_finished_semaphores.clear();

    for (auto &semaphore : renderer.vk.image_available_semaphores) {
        vkDestroySemaphore(renderer.vk.device, semaphore, nullptr);
    }
    renderer.vk.image_available_semaphores.clear();

    for (auto &fence : renderer.vk.in_flight_fences) {
        vkDestroyFence(renderer.vk.device, fence, nullptr);
    }
    renderer.vk.in_flight_fences.clear();
}

void core::Engine::destroy_descriptor_pool(core::Renderer &renderer) {
    vkDestroyDescriptorSetLayout(renderer.vk.device,
                                 renderer.pass.descriptor_set_layout, nullptr);
    vkDestroyDescriptorPool(renderer.vk.device, renderer.pass.descriptor_pool,
                            nullptr);
}

void core::Engine::destroy_pipelines(core::Renderer &renderer) {
    vkDestroyPipeline(renderer.vk.device, renderer.pass.pipeline, nullptr);
    vkDestroyPipelineLayout(renderer.vk.device, renderer.pass.pipeline_layout,
                            nullptr);
}

void core::Engine::destroy_render_targets(core::Renderer &renderer) {
    vkDestroyRenderPass(renderer.vk.device, renderer.pass.render_pass, nullptr);
}

void core::Engine::destroy_framebuffers(core::Renderer &renderer) {
    for (auto &framebuffer : renderer.pass.framebuffers) {
        vkDestroyFramebuffer(renderer.vk.device, framebuffer, nullptr);
    }
    renderer.pass.framebuffers.clear();
}

void core::Engine::destroy_vma(core::Renderer &renderer) {
    vmaDestroyAllocator(renderer.allocator);
}

void core::Engine::destroy_devices(core::Renderer &renderer) {
    vkDestroyDevice(renderer.vk.device, nullptr);
    vkDestroyInstance(renderer.vk.instance, nullptr);
}
