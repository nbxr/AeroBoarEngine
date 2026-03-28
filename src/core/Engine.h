#pragma once
#include "VkBootstrap.h"
#include <stdio.h>
#include <iostream>
#include <utility>
#define LOG_ERROR(value) std::cerr << value << std::endl
#define LOG_INFO(value) std::out << value << std::endl

namespace core {
struct Renderer;
class Engine {
  public:
    static bool initialize(core::Renderer &renderer);
    static vkb::Instance &init_vk_instance(core::Renderer &renderer,
                                           vkb::InstanceBuilder &builder);
    static bool init_surface(core::Renderer &renderer);
    static void render(core::Renderer &renderer);
    static void destroy(core::Renderer &renderer);

  private:
  // devices
    static void add_features(vkb::PhysicalDeviceSelector &selector);
    static std::pair<bool, vkb::PhysicalDevice> init_physical_device(core::Renderer &renderer,
                                                     vkb::Instance &inst);
    static std::pair<bool, vkb::Device> init_logical_device(core::Renderer &renderer,
                                            vkb::PhysicalDevice &phys);
    static bool init_graphics_queue(core::Renderer &renderer, vkb::Device &dev);
    static bool init_present_queue(core::Renderer &renderer, vkb::Device &dev);
    static bool init_transfer_queue(core::Renderer &renderer, vkb::Device &dev);
    static bool init_swapchain(core::Renderer &renderer, vkb::Device &dev);
    static bool init_render_pass(core::Renderer &renderer);
    static bool init_descriptor_pool(core::Renderer &renderer);
    static bool init_descriptor_set_layout(core::Renderer &renderer);
    static bool init_command_pool(core::Renderer &renderer);
    static bool init_command_buffers(core::Renderer &renderer);
    static bool init_framebuffers(core::Renderer &renderer);
    static bool init_sync_primitives(core::Renderer &renderer);
    static bool init_vulkan(core::Renderer &renderer);
    static bool init_vma(core::Renderer &renderer);
    static bool init_pipeline_layout(core::Renderer &renderer);
    static bool init_graphics_pipeline(core::Renderer &renderer);
    static void destroy_buffers(core::Renderer &renderer);
    static void destroy_render_pass(core::Renderer &renderer);
    static void destroy_vulkan(core::Renderer &renderer);
    static void destroy_vma(core::Renderer &renderer);
};
}; // namespace core