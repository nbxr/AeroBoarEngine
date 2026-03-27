#include "Engine.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void core::Engine::destroy(core::Renderer &renderer) {
    destroy_buffers(renderer);
    destroy_render_pass(renderer);
    destroy_vma(renderer);

    // vulkan instance
    destroy_vulkan(renderer);
}

void core::Engine::destroy_buffers(core::Renderer &renderer) {

}

void core::Engine::destroy_render_pass(core::Renderer &renderer) {
    vkDestroyRenderPass(renderer.vk.device, renderer.pass.render_pass, nullptr);
}

void core::Engine::destroy_vulkan(core::Renderer &renderer) {
    vkDestroySwapchainKHR(renderer.vk.device, renderer.vk.swapchain, nullptr);
    vkDestroyDevice(renderer.vk.device, nullptr);
    vkDestroyInstance(renderer.vk.instance, nullptr);
}

void core::Engine::destroy_vma(core::Renderer &renderer) {
    vmaDestroyAllocator(renderer.allocator);
}
