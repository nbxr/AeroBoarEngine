#pragma once
#include "VkBootstrap.h"
namespace core {
struct Renderer;
class Engine {
  public:
    static void initialize(core::Renderer &renderer);
    static vkb::Instance &init_vk_instance(core::Renderer &renderer,
                                           vkb::InstanceBuilder &builder);
    static void init_surface(core::Renderer &renderer);
    static void render(core::Renderer &renderer);
    static void destroy(core::Renderer &renderer);

  private:
  // devices
    static void add_features(vkb::PhysicalDeviceSelector &selector);
    static vkb::PhysicalDevice &init_physical_device(core::Renderer &renderer,
                                                     vkb::Instance &inst);
    static vkb::Device &init_logical_device(core::Renderer &renderer,
                                            vkb::PhysicalDevice &phys);
    static void init_graphics_queue(core::Renderer &renderer, vkb::Device &dev);
    static void init_present_queue(core::Renderer &renderer, vkb::Device &dev);
    static void init_transfer_queue(core::Renderer &renderer, vkb::Device &dev);
    static void init_swapchain(core::Renderer &renderer, vkb::Device &dev);
    static void init_render_pass(core::Renderer &renderer);
    static void init_descriptor_pool(core::Renderer &renderer);
    static void init_descriptor_set_layout(core::Renderer &renderer);
    static void init_command_pool(core::Renderer &renderer);
    static void init_command_buffers(core::Renderer &renderer);
    static void init_framebuffers(core::Renderer &renderer);
    static void init_sync_primitives(core::Renderer &renderer);
    static void init_vulkan(core::Renderer &renderer);
    static void init_vma(core::Renderer &renderer);
    static void init_pipeline_layout(core::Renderer &renderer);
    static void init_graphics_pipeline(core::Renderer &renderer);
    static void destroy_buffers(core::Renderer &renderer);
    static void destroy_render_pass(core::Renderer &renderer);
    static void destroy_vulkan(core::Renderer &renderer);
    static void destroy_vma(core::Renderer &renderer);
};
}; // namespace core