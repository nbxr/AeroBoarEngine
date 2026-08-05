#pragma once

#include "gfx/Material.h"
#include "gfx/DoubleBufferedBuffer.h"
#include <shared_mutex>
#include <stack>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace gfx {

class MaterialManager {
  public:
    MaterialManager() = default;
    ~MaterialManager();

    MaterialManager(const MaterialManager&) = delete;
    MaterialManager& operator=(const MaterialManager&) = delete;

    bool is_initialized() const;
    bool initialize(VkDevice device, VmaAllocator allocator, uint32_t initial_capacity);

    MaterialID create_material(const Material& material);
    void remove_material(MaterialID material_id);
    void update_material(MaterialID material_id, const Material& material);

    void update_buffers();
    void bind_descriptor(uint32_t binding_index, VkDescriptorSet target_set);
    void toggle_buffers();
    void shutdown();

    [[nodiscard]] uint32_t get_material_count() const { return material_count_; }
    [[nodiscard]] uint32_t get_material_flags(uint32_t material_id) const;
    [[nodiscard]] const Material* get_material(uint32_t material_id) const;

  private:
    VkDevice device_{VK_NULL_HANDLE};
    VmaAllocator allocator_{VK_NULL_HANDLE};

    DoubleBufferedBuffer buffers_{};
    std::vector<Material> cpu_materials_{};
    std::stack<MaterialID> recycle_cache_{};

    uint32_t material_count_ = 0;
    uint32_t growth_step_size_ = 50;

    mutable std::shared_mutex material_mutex_;
};

} // namespace gfx
