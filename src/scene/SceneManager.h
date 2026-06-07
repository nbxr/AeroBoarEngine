#pragma once

#include "gfx/AllocatedBuffer.h"
#include "scene/SceneInstance.h"
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
class SceneManager {
  private:
    VkDevice device{VK_NULL_HANDLE};
    VmaAllocator allocator{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};

    // Internal storage using AllocatedBuffer
    std::array<gfx::AllocatedBuffer, 2> instance_buffer{};

    // Indexes to control which buffers are used for uploading
    // and which are used for rendering
    uint32_t upload = 1;
    uint32_t render = 0;

    // CPU-side mirror for quick access/add/remove
    std::vector<SceneInstance> cpu_instances{};

    uint32_t instance_count = 0;
    std::array<uint32_t, 2> max_instances{0, 0};
    uint32_t growth_step_size = 50;

    mutable std::shared_mutex instance_mutex;

  public:
    SceneManager() = default;
    ~SceneManager();

    // Non-copyable
    SceneManager(const SceneManager &) = delete;
    SceneManager &operator=(const SceneManager &) = delete;

    // Common buffer management methods
    bool is_initialized();
    bool initialize(VkDevice device, VmaAllocator allocator,
                    VkDescriptorSet descriptor_set, uint32_t initial_capacity);

    bool add_instance(const SceneInstance &instance);
    void remove_instance(uint32_t index);
    void update_buffers();
    void bind_descriptor(uint32_t binding_index);
    void toggle_buffers();  // exposed for Engine load-time commit (double-buffer swap)
    
    [[nodiscard]] gfx::AllocatedBuffer &get_buffer() {
        return get_render_buffer();
    }
    
    [[nodiscard]] VkDescriptorSet get_descriptor_set() const {
        return descriptor_set;
    }

    // Returns the world transform of the first loaded SceneInstance (identity if none)
    [[nodiscard]] glm::mat4 get_first_instance_transform() const;

    // Returns a (center, radius) pair suitable for camera framing based on the
    // first instance's world-space AABB. Falls back to transform + fixed radius
    // if no valid AABB is available. Includes a small upward bias for better views.
    [[nodiscard]] std::pair<glm::vec3, float> get_first_instance_framing_sphere() const;

    // Debug / render accessors for drawing all scene geometry.
    // NOTE: lock-free; safe for read-only use after load_scene() completes (no mutation during render).
    [[nodiscard]] uint32_t get_instance_count() const { return instance_count; }
    [[nodiscard]] const SceneInstance &get_instance(uint32_t index) const { return cpu_instances[index]; }

    void shutdown();

  private:
    void resize_buffer(uint32_t new_capacity);
    [[nodiscard]] gfx::AllocatedBuffer &get_upload_buffer();
    [[nodiscard]] gfx::AllocatedBuffer &get_render_buffer();
};
} // namespace scene