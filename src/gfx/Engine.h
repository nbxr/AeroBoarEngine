#pragma once
#include "VkBootstrap.h"
#include "gfx/AllocatedImage.h"
#include "gfx/Light.h"
#include "gfx/Renderer.h"
#include "gfx/GpuTimestamps.h"
#include "physics/PhysicsWorld.h"
#include "scene/Camera.h"
#include "ecs/World.h"
#include "core/FrameStats.h"
#include "core/Log.h"
#include <glm/glm.hpp>
#include <iostream>
#include <stdio.h>
#include <string>
#include <utility>

namespace tinygltf {
class Model;
}

namespace gfx {
class Engine {
  public:
    Renderer renderer{};
    scene::Camera camera{};   // Desktop + future VR camera system
    physics::PhysicsWorld physics{};
    ecs::World ecs_world{};   // Entities / components (render still dual-writes GameObject)
    core::FrameStats frame_stats{};
    GpuTimestamps gpu_times{};


    bool initialize();
    bool init_vk_instance(vkb::InstanceBuilder &builder);
    bool init_surface();
    void render();
    void destroy();
    void recreate_swapchain();
    bool load_default_scene();
    bool load_scene(const std::string &scene_name);
    void cleanup_scene();

    // TDR / DWM composition drop / SURFACE_LOST: rebuild device + reload scene.
    // GLFW window is kept. Happy-path cost is zero (only runs after DEVICE_LOST).
    bool try_recover_gpu();
    void mark_device_lost(const char* where);

    // Lighting runtime API (mutations take effect on the next frame write).
    // Re-apply world pos/dir from TransformManager after propagate().
    void refresh_lights_from_transforms();
    bool set_light(uint32_t index, const gfx::Light& light);
    uint32_t add_light(const gfx::Light& light); // returns index, or MAX_LIGHTS on fail
    bool set_light_enabled(uint32_t index, bool enabled);

    // Hierarchy: call after set_local_matrix / set_parent (or animation).
    // Propagates dirty transforms, refreshes instances + GPU cull models for
    // the current frame slot (must run after that frame's fence wait).
    // Returns true if any world matrix changed.
    bool sync_scene_transforms();

    // Advance playing glTF clips → TransformManager locals (call before render).
    void update_animations(float delta_time);

    // Fixed-step Jolt: kinematic from transforms → step → dynamics to transforms.
    // Then kill-floor cull for dynamic bodies. Call after move + animations.
    void step_physics(float delta_time);

    // Build colliders from glTF KHR_physics_rigid_bodies + KHR_implicit_shapes.
    // Call after load_scene (roots already scaled by config worldScale). ECS
    // player → kinematic; dynamic mass scaled by worldScale^3.
    bool spawn_scene_physics(const tinygltf::Model& model);

    // After scene load: enable kill floor from config and/or scene AABB.
    // Sim-space Y (after worldScale). Dynamics below the plane are destroyed.
    void configure_kill_floor();

  private:
    // World policy (not per-entity ECS): drop dynamics below this Y.
    bool kill_floor_enabled_ = false;
    float kill_floor_y_ = -1000.0f;
    void process_kill_floor();

    // Same-frame Hi-Z occlusion (desktop-only extra prepass). Off by default;
    // Adreno/Quest builds force off (GMEM). Config: "occlusionCull": true.
    bool occlusion_cull_enabled_ = false;
    void configure_occlusion_cull();
    void configure_meshlet_cull();

    // Both FIF slots must see light/SH changes. Camera is written every frame.
    uint32_t lights_upload_mask_ = (1u << Renderer::MAX_FRAMES_IN_FLIGHT) - 1u;
    void mark_lights_dirty() {
        lights_upload_mask_ = (1u << Renderer::MAX_FRAMES_IN_FLIGHT) - 1u;
    }

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
    bool gpu_wait_idle();
    void poll_display_composition();
    void apply_present_modes(vkb::SwapchainBuilder& builder);

    std::string last_scene_name_;
    uint32_t gpu_recoveries_ = 0;
    bool dwm_composition_on_ = true;
    bool dwm_composition_known_ = false;
    bool init_render_pass();
    bool init_depth_prepass();
    bool init_msaa_color_image();
    bool init_depth_image();

    // Reusable creation helpers (used by both initial init and resize recovery)
    bool create_msaa_color_image(VkExtent2D extent, AllocatedImage& out_image);
    bool create_depth_image(VkExtent2D extent, AllocatedImage& out_image);
    bool create_resolved_depth_image(VkExtent2D extent, AllocatedImage& out_image);
    bool create_prepass_depth_image(VkExtent2D extent, AllocatedImage& out_image);
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
    void write_frame_lighting(uint32_t frame_index,
                              const glm::mat4* view_proj = nullptr);

    bool init_pipeline_layout();
    bool init_graphics_pipeline();
    bool init_depth_prepass_pipeline();
    bool init_shadow_pipeline();
    void configure_shadows(uint32_t& out_resolution);

    // Wire prepass depth → HZB copy sets and HZB → cull sets (idle only).
    void wire_hzb_descriptors();

    // Rebuild mesh_draw_infos + GpuCulling after runtime instance changes.
    bool rebuild_draw_batches();

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