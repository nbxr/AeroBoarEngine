#pragma once

#include "gfx/DoubleBufferedBuffer.h"
#include "gfx/MeshData.h"
#include "gfx/MeshPrimitiveSSBO.h"
#include "core/AABB.h"
#include <shared_mutex>
#include <stack>
#include <vector>
#include <vulkan/vulkan.h>

namespace gfx {

class MeshManager {
  public:
    MeshManager() = default;
    ~MeshManager();

    MeshManager(const MeshManager&) = delete;
    MeshManager& operator=(const MeshManager&) = delete;

    bool is_initialized() const;
    bool initialize(VkDevice device, VmaAllocator allocator, uint32_t initial_capacity);

    MeshPrimitiveID add_mesh(MeshData& mesh_data);

    void update_buffers();
    void bind_descriptor(uint32_t ssbo, uint32_t vertex, uint32_t index,
                         VkDescriptorSet target_set);
    void toggle_buffers();
    void shutdown();

    void clear_all_caches();

    [[nodiscard]] uint32_t get_primitive_count() const;
    [[nodiscard]] uint64_t get_total_vertex_count() const { return vertex_count_; }
    [[nodiscard]] uint64_t get_total_index_count() const { return index_count_; }
    [[nodiscard]] uint32_t get_primitive_vertex_offset(uint32_t index) const;
    [[nodiscard]] uint32_t get_primitive_index_offset(uint32_t index) const;
    [[nodiscard]] uint32_t get_primitive_index_count(uint32_t index) const;
    [[nodiscard]] core::AABB get_primitive_local_aabb(uint32_t index) const;

    // Runtime morph / skinning helpers: read/write full Vertex arrays for a prim.
    // write patches CPU cache + both double-buffer sides (host-visible).
    bool copy_primitive_vertices(uint32_t index, Vertex* out, uint32_t count) const;
    bool write_primitive_vertices(uint32_t index, const Vertex* data, uint32_t count);

    [[nodiscard]] AllocatedBuffer& get_render_index_buffer() {
        return index_buffers_.render();
    }
    [[nodiscard]] AllocatedBuffer& get_render_vertex_buffer() {
        return vertex_buffers_.render();
    }

  private:
    bool ensure_capacity();

    VkDevice device_{VK_NULL_HANDLE};
    VmaAllocator allocator_{VK_NULL_HANDLE};

    DoubleBufferedBuffer vertex_buffers_{};
    DoubleBufferedBuffer index_buffers_{};
    DoubleBufferedBuffer ssbo_buffers_{};

    std::vector<MeshData> mesh_cache_{};
    std::vector<MeshPrimitiveSSBO> mesh_ssbo_cache_{};
    std::stack<MeshPrimitiveID> recycle_cache_{};

    uint32_t mesh_count_ = 0;
    uint64_t index_count_ = 0;
    uint64_t vertex_count_ = 0;
    uint32_t growth_step_size_ = 50;

    mutable std::shared_mutex mesh_mutex_;
};

} // namespace gfx
