#include "MeshManager.h"
#include "core/BufferUtils.h"
#include <mutex>

gfx::MeshManager::~MeshManager() {
    if (is_initialized())
        shutdown();
}

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
    for (size_t i = 0; i < max_meshes.size(); i++) {
        this->max_meshes[i] = initial_capacity;
        this->max_indices[i] = initial_capacity * 1000;
        this->max_vertices[i] = initial_capacity * 100;
    }

    if (initial_capacity > 0)
        this->growth_step_size = initial_capacity;

    mesh_cache.reserve(initial_capacity);
    mesh_ssbo_cache.reserve(initial_capacity);

    // ssbo buffer
    VkDeviceSize ssbo_size = initial_capacity * sizeof(MeshPrimitiveSSBO);
    bool ssbo_render_initialized = core::BufferUtils::initialize_buffer(
        device, allocator, ssbo_size, get_render_ssbo_buffer());
    bool ssbo_upload_initialized = core::BufferUtils::initialize_buffer(
        device, allocator, ssbo_size, get_upload_ssbo_buffer());

    // vertex buffer
    VkDeviceSize vertex_size = initial_capacity * 100 * sizeof(Vertex);
    bool vertex_render_initialized = core::BufferUtils::initialize_buffer(
        device, allocator, vertex_size, get_render_vertex_buffer());
    bool vertex_upload_initialized = core::BufferUtils::initialize_buffer(
        device, allocator, vertex_size, get_upload_vertex_buffer());

    // index buffer
    VkDeviceSize index_size = initial_capacity * 1000 * sizeof(Index);
    bool index_render_initialized = core::BufferUtils::initialize_buffer(
        device, allocator, index_size, get_render_index_buffer());
    bool index_upload_initialized = core::BufferUtils::initialize_buffer(
        device, allocator, index_size, get_upload_index_buffer());

    return true;
}

MeshPrimitiveID gfx::MeshManager::add_mesh(gfx::MeshData &mesh_data) {
    std::scoped_lock lock(mesh_mutex);
    mesh_cache.push_back(std::move(mesh_data));
    mesh_count = static_cast<uint32_t>(mesh_cache.size());
    MeshPrimitiveID id = MeshPrimitiveID(mesh_count - 1);
    return id;
}

void gfx::MeshManager::update_buffers() {
    
}

void gfx::MeshManager::bind_descriptor(uint32_t ssbo, uint32_t vertex,
                                       uint32_t index) {
    std::shared_lock lock(mesh_mutex);
    // SSBO buffer
    core::BufferUtils::update_descriptor(
        device, get_render_ssbo_buffer(), descriptor_set,
        mesh_count * sizeof(MeshPrimitiveSSBO), ssbo);
    // Vertex buffer
    core::BufferUtils::update_descriptor(device, get_render_vertex_buffer(),
                                         descriptor_set,
                                         vertex_count * sizeof(Vertex), vertex);
    // Index buffer
    core::BufferUtils::update_descriptor(device, get_render_index_buffer(),
                                         descriptor_set,
                                         index_count * sizeof(Index), index);
}

void gfx::MeshManager::shutdown() {
    std::scoped_lock lock(mesh_mutex);
    core::BufferUtils::destroy_buffer(device, allocator,
                                      get_upload_index_buffer());
    core::BufferUtils::destroy_buffer(device, allocator,
                                      get_upload_vertex_buffer());
    core::BufferUtils::destroy_buffer(device, allocator,
                                      get_upload_ssbo_buffer());
    core::BufferUtils::destroy_buffer(device, allocator,
                                      get_render_index_buffer());
    core::BufferUtils::destroy_buffer(device, allocator,
                                      get_render_vertex_buffer());
    core::BufferUtils::destroy_buffer(device, allocator,
                                      get_render_ssbo_buffer());
    mesh_cache.clear();
    mesh_ssbo_cache.clear();
    while (!recycle_cache.empty())
        recycle_cache.pop();

    mesh_count = 0;
    index_count = 0;
    vertex_count = 0;

    max_meshes = {0, 0};
    max_indices = {0, 0};
    max_vertices = {0, 0};

    device = VK_NULL_HANDLE;
    allocator = VK_NULL_HANDLE;
    descriptor_set = VK_NULL_HANDLE;
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
