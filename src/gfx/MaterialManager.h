#pragma once

#include "gfx/Material.h"
#include "gfx/TextureInfo.h"
#include "gfx/AllocatedBuffer.h"
#include <array>
#include <memory>
#include <shared_mutex>
#include <stack>
#include <unordered_map>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace gfx {
class MaterialManager {
  private:
    VkDevice device{VK_NULL_HANDLE};
    VmaAllocator allocator{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};

    // Internal storage using AllocatedBuffer
    std::array<AllocatedBuffer, 2> material_buffer{};

    // Indexes to control which buffers are used for uploading
    // and which are used for rendering
    uint32_t upload = 1;
    uint32_t render = 0;

    // Material storage
    std::vector<Material> cpu_materials{};

    // Handles that can be reused from within cpu_materials
    std::stack<MaterialID> recycle_cache{};

    uint32_t material_count = 0;
    std::array<uint32_t, 2> max_materials{0, 0};
    uint32_t growth_step_size = 50;

    mutable std::shared_mutex material_mutex;

  public:
    MaterialManager() = default;
    ~MaterialManager();

    // Non-copyable
    MaterialManager(const MaterialManager &) = delete;
    MaterialManager &operator=(const MaterialManager &) = delete;

    // Common buffer management methods
    bool is_initialized();
    bool initialize(VkDevice device, VmaAllocator allocator,
                    VkDescriptorSet descriptor_set, uint32_t initial_capacity);

    // Create a new material and return its ID
    MaterialID create_material(const gfx::Material &material);
    void remove_material(const MaterialID material_id);

    // Update a material by its ID
    void update_material(MaterialID material_id, const gfx::Material &material);

    void update_buffers();
    void bind_descriptor(uint32_t binding_index);
    void toggle_buffers();  // exposed for Engine load-time commit (double-buffer swap)
    void shutdown();

  private:
    void resize_buffer(uint32_t new_capacity);
    [[nodiscard]] AllocatedBuffer &get_upload_buffer();
    [[nodiscard]] AllocatedBuffer &get_render_buffer();
};
}; // namespace gfx