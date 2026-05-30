#pragma once

#include "gfx/AllocatedBuffer.h"
#include "scene/SceneInstance.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <vma/vk_mem_alloc.h>
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

    // Temporary debug helper
    glm::mat4 get_debug_first_instance_transform() const;

    void shutdown();

  private:
    void resize_buffer(uint32_t new_capacity);
    [[nodiscard]] gfx::AllocatedBuffer &get_upload_buffer();
    [[nodiscard]] gfx::AllocatedBuffer &get_render_buffer();
};
} // namespace scene