#include "gfx/MeshManager.h"
#include "gfx/BufferUtils.h"
#include <cstring>
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
    bool ssbo_render_initialized = gfx::BufferUtils::initialize_buffer(
        device, allocator, ssbo_size, get_render_ssbo_buffer());
    bool ssbo_upload_initialized = gfx::BufferUtils::initialize_buffer(
        device, allocator, ssbo_size, get_upload_ssbo_buffer());

    // vertex buffer
    VkDeviceSize vertex_size = initial_capacity * 100 * sizeof(Vertex);
    bool vertex_render_initialized = gfx::BufferUtils::initialize_buffer(
        device, allocator, vertex_size, get_render_vertex_buffer());
    bool vertex_upload_initialized = gfx::BufferUtils::initialize_buffer(
        device, allocator, vertex_size, get_upload_vertex_buffer());

    // index buffer
    VkDeviceSize index_size = initial_capacity * 1000 * sizeof(Index);
    VkBufferUsageFlags indexExtra = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bool index_render_initialized = gfx::BufferUtils::initialize_buffer(
        device, allocator, index_size, get_render_index_buffer(), indexExtra);
    bool index_upload_initialized = gfx::BufferUtils::initialize_buffer(
        device, allocator, index_size, get_upload_index_buffer(), indexExtra);

    return true;
}

gfx::MeshPrimitiveID gfx::MeshManager::add_mesh(gfx::MeshData &mesh_data) {
    std::scoped_lock lock(mesh_mutex);
    mesh_cache.push_back(std::move(mesh_data));
    mesh_count = static_cast<uint32_t>(mesh_cache.size());
    MeshPrimitiveID id = MeshPrimitiveID(mesh_count - 1);
    return id;
}

void gfx::MeshManager::update_buffers() {
    std::scoped_lock lock(mesh_mutex);

    ensure_capacity();

    mesh_ssbo_cache.clear(); // prevent accumulation on re-uploads / dynamic adds

    uint32_t vertex_offset = 0;
    uint32_t index_offset = 0;

    for (const auto &mesh : mesh_cache) {
        MeshPrimitiveSSBO ssbo;
        ssbo.vertex_offset = vertex_offset;
        ssbo.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
        ssbo.index_offset = index_offset;
        ssbo.index_count = static_cast<uint32_t>(mesh.indices.size());
        mesh_ssbo_cache.push_back(ssbo);

        auto *vertex_dst = static_cast<Vertex *>(get_upload_vertex_buffer().mapped_data);
        auto *index_dst = static_cast<Index *>(get_upload_index_buffer().mapped_data);

        memcpy(vertex_dst + vertex_offset, mesh.vertices.data(),
               mesh.vertices.size() * sizeof(Vertex));
        memcpy(index_dst + index_offset, mesh.indices.data(),
               mesh.indices.size() * sizeof(Index));

        vertex_offset += ssbo.vertex_count;
        index_offset += ssbo.index_count;
    }

    auto *ssbo_dst = static_cast<MeshPrimitiveSSBO *>(get_upload_ssbo_buffer().mapped_data);
    memcpy(ssbo_dst, mesh_ssbo_cache.data(), mesh_ssbo_cache.size() * sizeof(MeshPrimitiveSSBO));

    vertex_count = vertex_offset;
    index_count = index_offset;
}

void gfx::MeshManager::bind_descriptor(uint32_t ssbo, uint32_t vertex,
                                       uint32_t index) {
    std::shared_lock lock(mesh_mutex);
    // SSBO buffer
    gfx::BufferUtils::update_descriptor(
        device, get_render_ssbo_buffer(), descriptor_set,
        mesh_count * sizeof(MeshPrimitiveSSBO), ssbo);
    // Vertex buffer
    gfx::BufferUtils::update_descriptor(device, get_render_vertex_buffer(),
                                         descriptor_set,
                                         vertex_count * sizeof(Vertex), vertex);
    // Index buffer
    gfx::BufferUtils::update_descriptor(device, get_render_index_buffer(),
                                         descriptor_set,
                                         index_count * sizeof(Index), index);
}

void gfx::MeshManager::shutdown() {
    std::scoped_lock lock(mesh_mutex);
    gfx::BufferUtils::destroy_buffer(device, allocator,
                                      get_upload_index_buffer());
    gfx::BufferUtils::destroy_buffer(device, allocator,
                                      get_upload_vertex_buffer());
    gfx::BufferUtils::destroy_buffer(device, allocator,
                                      get_upload_ssbo_buffer());
    gfx::BufferUtils::destroy_buffer(device, allocator,
                                      get_render_index_buffer());
    gfx::BufferUtils::destroy_buffer(device, allocator,
                                      get_render_vertex_buffer());
    gfx::BufferUtils::destroy_buffer(device, allocator,
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

bool gfx::MeshManager::ensure_capacity() {
    uint64_t required_vertex_count = 0;
    uint64_t required_index_count = 0;

    for (const auto &mesh : mesh_cache) {
        required_vertex_count += mesh.vertices.size();
        required_index_count += mesh.indices.size();
    }

    // SSBO count is just mesh count
    if (mesh_cache.size() > mesh_ssbo_cache.capacity() ||
        mesh_ssbo_cache.empty()) {
        uint32_t new_ssbo_capacity = mesh_cache.empty()
                                         ? (growth_step_size > 0 ? growth_step_size : 100)
                                         : static_cast<uint32_t>(mesh_cache.size() + growth_step_size);
        resize_mesh_buffer(new_ssbo_capacity);
    }

    // Vertex buffer
    if (vertex_buffer[upload].info.size < required_vertex_count * sizeof(Vertex)) {
        uint64_t new_vertex_capacity = required_vertex_count + (growth_step_size > 0 ? growth_step_size * 100 : 10000);
        resize_vertex_buffer(new_vertex_capacity);
    }

    // Index buffer
    if (index_buffer[upload].info.size < required_index_count * sizeof(Index)) {
        uint64_t new_index_capacity = required_index_count + (growth_step_size > 0 ? growth_step_size * 1000 : 100000);
        resize_index_buffer(new_index_capacity);
    }

    return true;
}

void gfx::MeshManager::resize_mesh_buffer(uint32_t new_capacity) {
    // Resize SSBO buffer
    uint64_t new_ssbo_size = static_cast<uint64_t>(new_capacity) * sizeof(MeshPrimitiveSSBO);
    if (mesh_ssbo_cache.empty()) {
        gfx::BufferUtils::resize_buffer(
            device, allocator, new_ssbo_size,
            get_upload_ssbo_buffer(), nullptr, 0);
    } else {
        gfx::BufferUtils::resize_buffer(
            device, allocator, new_ssbo_size,
            get_upload_ssbo_buffer(),
            mesh_ssbo_cache.data(),
            mesh_ssbo_cache.size() * sizeof(MeshPrimitiveSSBO));
    }
}

void gfx::MeshManager::resize_vertex_buffer(uint64_t new_capacity) {
    if (new_capacity == 0) return;

    VkDeviceSize new_size = new_capacity * sizeof(Vertex);
    bool need_copy = false;

    if (!get_upload_vertex_buffer().buffer ||
        get_upload_vertex_buffer().info.size < new_size) {
        need_copy = true;
    }

    if (need_copy) {
        gfx::BufferUtils::resize_buffer(
            device, allocator, new_size,
            get_upload_vertex_buffer(),
            vertex_count > 0 ? get_upload_vertex_buffer().mapped_data : nullptr,
            need_copy ? vertex_count * sizeof(Vertex) : 0);
    }
}

void gfx::MeshManager::resize_index_buffer(uint64_t new_capacity) {
    if (new_capacity == 0) return;

    VkDeviceSize new_size = new_capacity * sizeof(Index);
    bool need_copy = false;

    if (!get_upload_index_buffer().buffer ||
        get_upload_index_buffer().info.size < new_size) {
        need_copy = true;
    }

    if (need_copy) {
        gfx::BufferUtils::resize_buffer(
            device, allocator, new_size,
            get_upload_index_buffer(),
            index_count > 0 ? get_upload_index_buffer().mapped_data : nullptr,
            need_copy ? index_count * sizeof(Index) : 0,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }
}


gfx::AllocatedBuffer &gfx::MeshManager::get_upload_vertex_buffer() {
    return vertex_buffer[upload];
}

gfx::AllocatedBuffer &gfx::MeshManager::get_render_vertex_buffer() {
    return vertex_buffer[render];
}

gfx::AllocatedBuffer &gfx::MeshManager::get_upload_index_buffer() {
    return index_buffer[upload];
}

gfx::AllocatedBuffer &gfx::MeshManager::get_render_index_buffer() {
    return index_buffer[render];
}

gfx::AllocatedBuffer &gfx::MeshManager::get_upload_ssbo_buffer() {
    return ssbo_buffer[upload];
}

gfx::AllocatedBuffer &gfx::MeshManager::get_render_ssbo_buffer() {
    return ssbo_buffer[render];
}

uint32_t gfx::MeshManager::get_first_primitive_vertex_offset() const {
    if (mesh_ssbo_cache.empty())
        return 0;
    return mesh_ssbo_cache[0].vertex_offset;
}

uint32_t gfx::MeshManager::get_first_primitive_index_offset() const {
    if (mesh_ssbo_cache.empty())
        return 0;
    return mesh_ssbo_cache[0].index_offset;
}

uint32_t gfx::MeshManager::get_first_primitive_index_count() const {
    if (mesh_ssbo_cache.empty())
        return 0;
    return mesh_ssbo_cache[0].index_count;
}

// Debug: full range for the very first mesh (sum of all its primitives)
uint32_t gfx::MeshManager::get_first_mesh_vertex_offset() const {
    if (mesh_cache.empty() || mesh_ssbo_cache.empty())
        return 0;
    // First mesh starts at the first primitive
    return mesh_ssbo_cache[0].vertex_offset;
}

uint32_t gfx::MeshManager::get_first_mesh_vertex_count() const {
    // In the current data model, each MeshData entry is already one complete mesh.
    // So "first mesh" == first entry in the cache.
    if (mesh_ssbo_cache.empty())
        return 0;
    return mesh_ssbo_cache[0].vertex_count;
}

uint32_t gfx::MeshManager::get_first_mesh_index_offset() const {
    if (mesh_cache.empty() || mesh_ssbo_cache.empty())
        return 0;
    return mesh_ssbo_cache[0].index_offset;
}

uint32_t gfx::MeshManager::get_first_mesh_index_count() const {
    if (mesh_ssbo_cache.empty())
        return 0;
    return mesh_ssbo_cache[0].index_count;
}

uint32_t gfx::MeshManager::get_primitive_count() const {
    return static_cast<uint32_t>(mesh_ssbo_cache.size());
}

uint32_t gfx::MeshManager::get_primitive_vertex_offset(uint32_t index) const {
    if (index >= mesh_ssbo_cache.size())
        return 0;
    return mesh_ssbo_cache[index].vertex_offset;
}

uint32_t gfx::MeshManager::get_primitive_index_offset(uint32_t index) const {
    if (index >= mesh_ssbo_cache.size())
        return 0;
    return mesh_ssbo_cache[index].index_offset;
}

uint32_t gfx::MeshManager::get_primitive_index_count(uint32_t index) const {
    if (index >= mesh_ssbo_cache.size())
        return 0;
    return mesh_ssbo_cache[index].index_count;
}
