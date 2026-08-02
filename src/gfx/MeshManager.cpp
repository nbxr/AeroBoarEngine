#include "gfx/MeshManager.h"
#include "gfx/BufferUtils.h"
#include <cstring>
#include <mutex>

namespace gfx {

MeshManager::~MeshManager() {
    if (is_initialized())
        shutdown();
}

bool MeshManager::is_initialized() const {
    return device_ != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE;
}

bool MeshManager::initialize(VkDevice device, VmaAllocator allocator,
                             uint32_t initial_capacity) {
    std::scoped_lock lock(mesh_mutex_);
    device_ = device;
    allocator_ = allocator;
    mesh_count_ = 0;
    if (initial_capacity > 0)
        growth_step_size_ = initial_capacity;

    mesh_cache_.reserve(initial_capacity);
    mesh_ssbo_cache_.reserve(initial_capacity);
    ssbo_buffers_.set_max_elements_both(initial_capacity);

    const VkDeviceSize ssbo_size =
        static_cast<VkDeviceSize>(initial_capacity) * sizeof(MeshPrimitiveSSBO);
    const VkDeviceSize vertex_size =
        static_cast<VkDeviceSize>(initial_capacity) * 100ull * sizeof(Vertex);
    const VkDeviceSize index_size =
        static_cast<VkDeviceSize>(initial_capacity) * 1000ull * sizeof(Index);

    const bool ok_ssbo = ssbo_buffers_.initialize(device, allocator, ssbo_size);
    const bool ok_vtx = vertex_buffers_.initialize(
        device, allocator, vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    const bool ok_idx = index_buffers_.initialize(
        device, allocator, index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    return ok_ssbo && ok_vtx && ok_idx;
}

MeshPrimitiveID MeshManager::add_mesh(MeshData& mesh_data) {
    std::scoped_lock lock(mesh_mutex_);
    mesh_cache_.push_back(std::move(mesh_data));
    mesh_count_ = static_cast<uint32_t>(mesh_cache_.size());
    return MeshPrimitiveID(mesh_count_ - 1);
}

void MeshManager::update_buffers() {
    std::scoped_lock lock(mesh_mutex_);

    ensure_capacity();

    mesh_ssbo_cache_.clear();

    uint32_t vertex_offset = 0;
    uint32_t index_offset = 0;

    auto* vertex_dst =
        static_cast<Vertex*>(vertex_buffers_.upload().mapped_data);
    auto* index_dst = static_cast<Index*>(index_buffers_.upload().mapped_data);

    for (const auto& mesh : mesh_cache_) {
        MeshPrimitiveSSBO ssbo{};
        ssbo.vertex_offset = vertex_offset;
        ssbo.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
        ssbo.index_offset = index_offset;
        ssbo.index_count = static_cast<uint32_t>(mesh.indices.size());
        mesh_ssbo_cache_.push_back(ssbo);

        if (vertex_dst && !mesh.vertices.empty()) {
            std::memcpy(vertex_dst + vertex_offset, mesh.vertices.data(),
                        mesh.vertices.size() * sizeof(Vertex));
        }
        if (index_dst && !mesh.indices.empty()) {
            std::memcpy(index_dst + index_offset, mesh.indices.data(),
                        mesh.indices.size() * sizeof(Index));
        }

        vertex_offset += ssbo.vertex_count;
        index_offset += ssbo.index_count;
    }

    if (!mesh_ssbo_cache_.empty()) {
        ssbo_buffers_.upload_memcpy(
            mesh_ssbo_cache_.data(),
            mesh_ssbo_cache_.size() * sizeof(MeshPrimitiveSSBO));
    }

    vertex_count_ = vertex_offset;
    index_count_ = index_offset;
}

void MeshManager::bind_descriptor(uint32_t ssbo, uint32_t vertex, uint32_t index,
                                  VkDescriptorSet target_set) {
    std::shared_lock lock(mesh_mutex_);
    ssbo_buffers_.bind_render_descriptor(
        device_, target_set, ssbo, mesh_count_ * sizeof(MeshPrimitiveSSBO));
    BufferUtils::update_descriptor(device_, vertex_buffers_.render(), target_set,
                                   vertex_count_ * sizeof(Vertex), vertex);
    BufferUtils::update_descriptor(device_, index_buffers_.render(), target_set,
                                   index_count_ * sizeof(Index), index);
}

void MeshManager::shutdown() {
    std::scoped_lock lock(mesh_mutex_);
    vertex_buffers_.destroy(device_, allocator_);
    index_buffers_.destroy(device_, allocator_);
    ssbo_buffers_.destroy(device_, allocator_);

    mesh_cache_.clear();
    mesh_ssbo_cache_.clear();
    while (!recycle_cache_.empty())
        recycle_cache_.pop();

    mesh_count_ = 0;
    index_count_ = 0;
    vertex_count_ = 0;
    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

void MeshManager::clear_all_caches() {
    std::scoped_lock lock(mesh_mutex_);
    mesh_cache_.clear();
    mesh_ssbo_cache_.clear();
    while (!recycle_cache_.empty())
        recycle_cache_.pop();
    mesh_count_ = 0;
    index_count_ = 0;
    vertex_count_ = 0;
}

void MeshManager::toggle_buffers() {
    vertex_buffers_.toggle();
    index_buffers_.toggle();
    ssbo_buffers_.toggle();
}

bool MeshManager::ensure_capacity() {
    uint64_t required_vertex_count = 0;
    uint64_t required_index_count = 0;
    for (const auto& mesh : mesh_cache_) {
        required_vertex_count += mesh.vertices.size();
        required_index_count += mesh.indices.size();
    }

    const uint32_t mesh_n = static_cast<uint32_t>(mesh_cache_.size());
    ssbo_buffers_.ensure_element_capacity(
        device_, allocator_, mesh_n > 0 ? mesh_n : 1u, sizeof(MeshPrimitiveSSBO),
        growth_step_size_,
        mesh_ssbo_cache_.empty() ? nullptr : mesh_ssbo_cache_.data(),
        mesh_ssbo_cache_.size() * sizeof(MeshPrimitiveSSBO));

    const VkDeviceSize vtx_needed = required_vertex_count * sizeof(Vertex);
    const VkDeviceSize vtx_pad =
        static_cast<VkDeviceSize>(growth_step_size_ > 0 ? growth_step_size_ * 100
                                                        : 10000) *
        sizeof(Vertex);
    vertex_buffers_.ensure_byte_capacity(
        device_, allocator_, vtx_needed, vtx_pad,
        vertex_count_ > 0 ? vertex_buffers_.upload().mapped_data : nullptr,
        vertex_count_ * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    const VkDeviceSize idx_needed = required_index_count * sizeof(Index);
    const VkDeviceSize idx_pad =
        static_cast<VkDeviceSize>(growth_step_size_ > 0 ? growth_step_size_ * 1000
                                                        : 100000) *
        sizeof(Index);
    index_buffers_.ensure_byte_capacity(
        device_, allocator_, idx_needed, idx_pad,
        index_count_ > 0 ? index_buffers_.upload().mapped_data : nullptr,
        index_count_ * sizeof(Index), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    return true;
}

uint32_t MeshManager::get_primitive_count() const {
    return static_cast<uint32_t>(mesh_ssbo_cache_.size());
}

uint32_t MeshManager::get_primitive_vertex_offset(uint32_t index) const {
    if (index >= mesh_ssbo_cache_.size())
        return 0;
    return mesh_ssbo_cache_[index].vertex_offset;
}

uint32_t MeshManager::get_primitive_index_offset(uint32_t index) const {
    if (index >= mesh_ssbo_cache_.size())
        return 0;
    return mesh_ssbo_cache_[index].index_offset;
}

uint32_t MeshManager::get_primitive_index_count(uint32_t index) const {
    if (index >= mesh_ssbo_cache_.size())
        return 0;
    return mesh_ssbo_cache_[index].index_count;
}

core::AABB MeshManager::get_primitive_local_aabb(uint32_t index) const {
    std::shared_lock lock(mesh_mutex_);
    if (index >= mesh_cache_.size())
        return core::AABB{};
    return mesh_cache_[index].local_aabb;
}

} // namespace gfx
