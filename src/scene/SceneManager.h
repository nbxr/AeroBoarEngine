#pragma once

#include "gfx/AllocatedBuffer.h"
#include "scene/GameObject.h"
#include "scene/RenderMesh.h"
#include "scene/SceneInstance.h"
#include "scene/TransformManager.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace scene {

// Orchestrates GameObject / RenderMesh / TransformManager and (legacy) flat
// SceneInstance GPU buffers. New code should use the GameObject registry APIs.
class SceneManager {
  private:
    VkDevice device{VK_NULL_HANDLE};
    VmaAllocator allocator{VK_NULL_HANDLE};

    std::array<gfx::AllocatedBuffer, 2> instance_buffer{};
    uint32_t upload = 1;
    uint32_t render = 0;

    // Legacy flat list (still uploaded for optional GPU use / debugging)
    std::vector<SceneInstance> cpu_instances{};
    uint32_t instance_count = 0;
    std::array<uint32_t, 2> max_instances{0, 0};
    uint32_t growth_step_size = 50;

    // New scene model
    TransformManager transforms_{};
    std::vector<GameObject> game_objects_{};
    std::vector<RenderMesh> render_meshes_{};

    mutable std::shared_mutex instance_mutex;

  public:
    SceneManager() = default;
    ~SceneManager();

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    bool is_initialized();
    bool initialize(VkDevice device, VmaAllocator allocator, uint32_t initial_capacity);

    // --- New registry API ---
    TransformManager& transforms() { return transforms_; }
    const TransformManager& transforms() const { return transforms_; }

    // Create a GameObject with a root transform set to `world`. Returns index.
    uint32_t create_game_object(const glm::mat4& world, uint32_t gltf_node_index = ~0u);

    // Append a RenderMesh owned by game_object_index (shares GO root transform by default).
    uint32_t add_render_mesh(uint32_t game_object_index, uint32_t mesh_index,
                             uint32_t material_index, const core::AABB& local_aabb,
                             uint32_t transform_index = ~0u);

    [[nodiscard]] uint32_t game_object_count() const {
        return static_cast<uint32_t>(game_objects_.size());
    }
    [[nodiscard]] uint32_t render_mesh_count() const {
        return static_cast<uint32_t>(render_meshes_.size());
    }
    [[nodiscard]] const GameObject& get_game_object(uint32_t i) const {
        return game_objects_[i];
    }
    [[nodiscard]] const RenderMesh& get_render_mesh(uint32_t i) const {
        return render_meshes_[i];
    }
    [[nodiscard]] const std::vector<RenderMesh>& render_meshes() const {
        return render_meshes_;
    }

    // --- Legacy SceneInstance path (still dual-written on add_render_mesh) ---
    bool add_instance(const SceneInstance& instance);
    void remove_instance(uint32_t index);
    void update_buffers();
    void bind_descriptor(uint32_t binding_index, VkDescriptorSet target_set);
    void toggle_buffers();

    [[nodiscard]] gfx::AllocatedBuffer& get_buffer() { return get_render_buffer(); }

    [[nodiscard]] glm::mat4 get_first_instance_transform() const;
    [[nodiscard]] std::pair<glm::vec3, float> get_first_instance_framing_sphere() const;
    // Union of all render-mesh world AABBs (preferred for camera frame).
    [[nodiscard]] std::pair<glm::vec3, float> get_scene_framing_sphere() const;

    [[nodiscard]] uint32_t get_instance_count() const { return instance_count; }
    [[nodiscard]] const SceneInstance& get_instance(uint32_t index) const {
        return cpu_instances[index];
    }

    void shutdown();
    void clear_scene_data(); // clears GO/RM/transforms + instances (keeps Vulkan buffers)

  private:
    void resize_buffer(uint32_t new_capacity);
    [[nodiscard]] gfx::AllocatedBuffer& get_upload_buffer();
    [[nodiscard]] gfx::AllocatedBuffer& get_render_buffer();
};

} // namespace scene
