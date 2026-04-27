#include "MeshManager.h"
#include "core/BufferUtils.h"
#include <mutex>

bool gfx::MeshManager::is_initialized() {
    return device != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE;
}

bool gfx::MeshManager::initialize(VkDevice device, VmaAllocator allocator,
                                  VkDescriptorSet descriptor_set,
                                  uint32_t initial_capacity) {
    std::scoped_lock lock(mesh_mutex);
    this->device = device;
    this->allocator = allocator;
    this->descriptor_set = descriptor_set;
    this->mesh_count = 0;
    for (size_t i = 0; i < max_meshes.size(); i++)
        this->max_meshes[i] = initial_capacity;
    if (initial_capacity > 0)
        this->growth_step_size = initial_capacity;

        mesh_cache.reserve(initial_capacity);
        mesh_ssbo_cache.reserve(initial_capacity);

        VkDeviceSize size = initial_capacity * sizeof(MeshPrimitiveSSBO);
    bool render_initialized = core::BufferUtils::initialize_buffer(
        device, allocator, size, get_render_ssbo_buffer());
    bool upload_initialized = core::BufferUtils::initialize_buffer(
        device, allocator, size, get_upload_ssbo_buffer());


    return false;
}

MeshPrimitiveID gfx::MeshManager::add_mesh(gfx::MeshData &mesh_data) {
    std::scoped_lock lock(mesh_mutex);
    mesh_cache.push_back(std::move(mesh_data));
    mesh_count = static_cast<uint32_t>(mesh_cache.size());
    MeshPrimitiveID id = MeshPrimitiveID(mesh_count - 1);
    pending_upload.push_back(id);
    return id;
}

void gfx::MeshManager::toggle_buffers() {
    render ^= 1;
    upload = render ^ 1;
}

core::AllocatedBuffer &gfx::MeshManager::get_upload_vertex_buffer() {
    return vertex_buffer[upload];
}

core::AllocatedBuffer &gfx::MeshManager::get_render_vertex_buffer() {
    return vertex_buffer[render];
}

core::AllocatedBuffer &gfx::MeshManager::get_upload_index_buffer() {
    return index_buffer[upload];
}

core::AllocatedBuffer &gfx::MeshManager::get_render_index_buffer() {
    return index_buffer[render];
}

core::AllocatedBuffer &gfx::MeshManager::get_upload_ssbo_buffer() {
    return ssbo_buffer[upload];
}

core::AllocatedBuffer &gfx::MeshManager::get_render_ssbo_buffer() {
    return ssbo_buffer[render];
}
