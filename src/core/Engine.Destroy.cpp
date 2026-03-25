#include "Engine.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void core::Engine::destroy(Renderer &renderer) {
    destroy_render_pass(renderer);
    destroy_vma(renderer);
    destroy_vulkan(renderer);
}

void core::Engine::destroy_render_pass(core::Renderer &renderer) {
    vkDestroyRenderPass(renderer.vk.device, renderer.pass.render_pass, nullptr);
}

void core::Engine::destroy_vulkan(Renderer &renderer) {
    vkb::destroy_swapchain(renderer.vk.swapchain);
    vkb::destroy_device(renderer.vk.device);
    vkb::destroy_instance(renderer.vk.instance);
}

void core::Engine::destroy_vma(Renderer &renderer) {
    vmaDestroyAllocator(renderer.allocator);
}