#pragma once
#include "VkBootstrap.h"
#include "gfx/AllocatedImage.h"
#include "gfx/Renderer.h"
#include "scene/Camera.h"
#include "core/Log.h"
#include <glm/glm.hpp>
#include <iostream>
#include <stdio.h>
#include <utility>

namespace gfx {
class Engine {
  public:
    Renderer renderer{};
    scene::Camera camera{};   // Desktop + future VR camera system


    bool initialize();
    bool init_vk_instance(vkb::InstanceBuilder &builder);
    bool init_surface();
    void render();
    void destroy();
    void recreate_swapchain();
    bool load_default_scene();
    bool load_scene(const std::string &scene_name);
    void cleanup_scene();

  private:
    // devices
    void add_features(vkb::PhysicalDeviceSelector &selector);
    std::pair<bool, vkb::PhysicalDevice>
    init_physical_device();
    std::pair<bool, vkb::Device>
    init_logical_device(vkb::PhysicalDevice &phys);
    void select_depth_format(vkb::PhysicalDevice &phys);
    void select_sample_counts(vkb::PhysicalDevice &phys);
    bool init_graphics_queue(vkb::Device &dev);
    bool init_present_queue(vkb::Device &dev);
    bool init_transfer_queue(vkb::Device &dev);
    bool init_swapchain(vkb::Device &dev);
    bool init_render_pass();
    bool init_msaa_color_image();
    bool init_depth_image();

    // Reusable creation helpers (used by both initial init and resize recovery)
    bool create_msaa_color_image(VkExtent2D extent, AllocatedImage& out_image);
    bool create_depth_image(VkExtent2D extent, AllocatedImage& out_image);
    bool init_descriptor_pool();
    bool init_descriptor_set_layout();
    bool init_bindless_descriptor_set();
    bool init_command_pool();
    bool init_command_buffers();
    bool init_framebuffers();
    bool init_sync_primitives();
    bool init_resource_managers();
    bool init_vulkan();
    bool init_vma();

    // Bind FrameConstants UBO + lights SSBO for every per-frame descriptor set.
    void bind_frame_lighting_to_all_sets();

    // Write active lights + constants into the mapped buffers for one frame slot.
    void write_frame_lighting(uint32_t frame_index);

    // Frustum-cull RenderMeshes, pack DrawInstanceGPU + indirect commands for this frame.
    void prepare_culled_draws(uint32_t frame_index, const glm::mat4& view_proj);

    bool init_pipeline_layout();
    bool init_graphics_pipeline();

    void destroy_sync_primitives();
    void destroy_descriptor_pool();
    void destroy_pipelines();
    void destroy_render_targets();
    void destroy_framebuffers();
    void destroy_images();
    void destroy_buffers();
    void destroy_command_buffers();
    void destroy_swapchain();
    void destroy_resource_managers();
    void destroy_vma();
    void destroy_devices();
};
} // namespace gfx