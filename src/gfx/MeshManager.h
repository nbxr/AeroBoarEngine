#pragma once


#include "gfx/MeshData.h"
#include "gfx/MeshPrimitiveSSBO.h"
#include "gfx/AllocatedBuffer.h"
#include "core/AABB.h"
#include <array>
#include <shared_mutex>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace gfx {
class MeshManager {
  private:
    VkDevice device{VK_NULL_HANDLE};
    VmaAllocator allocator{VK_NULL_HANDLE};

    // Internal storage using AllocatedBuffer
    std::array<AllocatedBuffer, 2> vertex_buffer{};
    std::array<AllocatedBuffer, 2> index_buffer{};
    std::array<AllocatedBuffer, 2> ssbo_buffer{};

    // Indexes to control which buffers are used for uploading
    // and which are used for rendering
    uint32_t upload = 1;
    uint32_t render = 0;

    // Mesh storage
    std::vector<gfx::MeshData> mesh_cache{};
    std::vector<gfx::MeshPrimitiveSSBO> mesh_ssbo_cache{};

    // Handles that can be reused from within mesh_cache
    std::stack<MeshPrimitiveID> recycle_cache{};

    uint32_t mesh_count = 0;
    uint64_t index_count = 0L;
    uint64_t vertex_count = 0L;

    std::array<uint32_t, 2> max_meshes{0, 0};
    std::array<uint32_t, 2> max_indices{0, 0};
    std::array<uint32_t, 2> max_vertices{0, 0};

    uint32_t growth_step_size = 50;

    mutable std::shared_mutex mesh_mutex;

  public:
    MeshManager() = default;
    ~MeshManager();

    // Non-copyable
    MeshManager(const MeshManager &) = delete;
    MeshManager &operator=(const MeshManager &) = delete;

    // Common buffer management methods
    bool is_initialized();
    bool initialize(VkDevice device, VmaAllocator allocator,
                    uint32_t initial_capacity);

    MeshPrimitiveID add_mesh(gfx::MeshData &mesh_data);

    void update_buffers();
    void bind_descriptor(uint32_t ssbo, uint32_t vertex, uint32_t index, VkDescriptorSet target_set);
    void toggle_buffers();  // exposed for Engine load-time commit (double-buffer swap)
    void shutdown();

    // Force clear CPU-side mesh data (scene reload / re-upload).
    void clear_all_caches();

    // Per-primitive accessors (GPU cull / instanced draw path)
    [[nodiscard]] uint32_t get_primitive_count() const;
    [[nodiscard]] uint64_t get_total_vertex_count() const { return vertex_count; }
    [[nodiscard]] uint64_t get_total_index_count() const { return index_count; }
    [[nodiscard]] uint32_t get_primitive_vertex_offset(uint32_t index) const;
    [[nodiscard]] uint32_t get_primitive_index_offset(uint32_t index) const;
    [[nodiscard]] uint32_t get_primitive_index_count(uint32_t index) const;

    // Returns the local-space AABB for a given primitive (as computed during GLTF load)
    [[nodiscard]] core::AABB get_primitive_local_aabb(uint32_t index) const;

    [[nodiscard]] AllocatedBuffer &get_render_index_buffer();
    [[nodiscard]] AllocatedBuffer &get_render_vertex_buffer();

  private:
    bool ensure_capacity();
    void resize_mesh_buffer(uint32_t new_capacity);
    void resize_vertex_buffer(uint64_t new_capacity);
    void resize_index_buffer(uint64_t new_capacity);

    [[nodiscard]] AllocatedBuffer &get_upload_vertex_buffer();
    [[nodiscard]] AllocatedBuffer &get_upload_index_buffer();
    [[nodiscard]] AllocatedBuffer &get_upload_ssbo_buffer();
    [[nodiscard]] AllocatedBuffer &get_render_ssbo_buffer();
};
} // namespace gfx