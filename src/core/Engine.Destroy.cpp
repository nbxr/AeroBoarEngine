#include "Engine.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void Core::Engine::destroy(Renderer &renderer) {
    destroy_vma(renderer);
    destroy_vulkan(renderer);
}

void Core::Engine::destroy_vulkan(Renderer &renderer) {
    vkb::destroy_swapchain(renderer.vk.swapchain);
    vkb::destroy_device(renderer.vk.device);
    vkb::destroy_instance(renderer.vk.instance);
}

void Core::Engine::destroy_vma(Renderer &renderer) {
    vmaDestroyAllocator(renderer.allocator);
}