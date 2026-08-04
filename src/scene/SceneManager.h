#pragma once

#include "gfx/DoubleBufferedBuffer.h"
#include "scene/Animation.h"
#include "scene/GameObject.h"
#include "scene/Morph.h"
#include "scene/RenderMesh.h"
#include "scene/SceneInstance.h"
#include "scene/Skin.h"
#include "scene/TransformManager.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <shared_mutex>
#include <utility>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace scene {

// Orchestrates GameObject / RenderMesh / TransformManager and (legacy) flat
// SceneInstance GPU buffers. New code should use the GameObject registry APIs.
class SceneManager {
  public:
    SceneManager() = default;
    ~SceneManager();

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    bool is_initialized() const;
    bool initialize(VkDevice device, VmaAllocator allocator, uint32_t initial_capacity);

    TransformManager& transforms() { return transforms_; }
    const TransformManager& transforms() const { return transforms_; }

    AnimationSystem& animations() { return animations_; }
    const AnimationSystem& animations() const { return animations_; }

    SkinSystem& skins() { return skins_; }
    const SkinSystem& skins() const { return skins_; }

    MorphSystem& morphs() { return morphs_; }
    const MorphSystem& morphs() const { return morphs_; }

    // glTF node index → TransformManager index (kInvalid if none). Filled at load.
    std::vector<uint32_t>& gltf_node_to_transform() { return gltf_node_to_transform_; }
    const std::vector<uint32_t>& gltf_node_to_transform() const {
        return gltf_node_to_transform_;
    }

    // Create a GameObject that owns `root_transform_index` (already allocated in
    // TransformManager with local + parent). Call transforms().propagate() after
    // the full hierarchy is built.
    uint32_t create_game_object(uint32_t root_transform_index,
                                uint32_t gltf_node_index = ~0u,
                                uint32_t skin_index = ~0u);

    // Append a RenderMesh owned by game_object_index (shares GO root transform by default).
    uint32_t add_render_mesh(uint32_t game_object_index, uint32_t mesh_index,
                             uint32_t material_index, const core::AABB& local_aabb,
                             uint32_t transform_index = ~0u);

    // Re-sync dual-written SceneInstance worlds from TransformManager after
    // propagate(). Call before update_buffers() / GpuCulling::build_scene.
    void refresh_instance_worlds();

    // If any transform is dirty: propagate hierarchy, refresh SceneInstance
    // worlds. Returns true if worlds changed (caller should refresh GPU cull).
    bool sync_transforms();

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

    [[nodiscard]] gfx::AllocatedBuffer& get_buffer() { return instance_buffers_.render(); }

    [[nodiscard]] glm::mat4 get_first_instance_transform() const;
    [[nodiscard]] std::pair<glm::vec3, float> get_first_instance_framing_sphere() const;
    [[nodiscard]] std::pair<glm::vec3, float> get_scene_framing_sphere() const;

    [[nodiscard]] uint32_t get_instance_count() const { return instance_count_; }
    [[nodiscard]] const SceneInstance& get_instance(uint32_t index) const {
        return cpu_instances_[index];
    }

    void shutdown();
    void clear_scene_data(); // clears GO/RM/transforms + instances (keeps Vulkan buffers)

  private:
    VkDevice device_{VK_NULL_HANDLE};
    VmaAllocator allocator_{VK_NULL_HANDLE};

    gfx::DoubleBufferedBuffer instance_buffers_{};
    std::vector<SceneInstance> cpu_instances_{};
    uint32_t instance_count_ = 0;
    uint32_t growth_step_size_ = 50;

    TransformManager transforms_{};
    AnimationSystem animations_{};
    SkinSystem skins_{};
    MorphSystem morphs_{};
    std::vector<uint32_t> gltf_node_to_transform_{};
    std::vector<GameObject> game_objects_{};
    std::vector<RenderMesh> render_meshes_{};

    mutable std::shared_mutex instance_mutex_;
};

} // namespace scene
