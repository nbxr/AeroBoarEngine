#include "MaterialManager.h"
#include "core/BufferUtils.h"
#include <cstring>
#include <mutex>

gfx::MaterialManager::~MaterialManager() { shutdown(); }

bool gfx::MaterialManager::is_initialized() {
    return device != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE;
}

bool gfx::MaterialManager::initialize(VkDevice device, VmaAllocator allocator,
                                      VkDescriptorSet descriptor_set,
                                      uint32_t initial_capacity) {
    this->device = device;
    this->allocator = allocator;
    this->descriptor_set = descriptor_set;
    for (size_t i = 0; i < max_materials.size(); i++)
        this->max_materials[i] = initial_capacity;
    this->material_count = 0;
    if (initial_capacity > 0)
        this->growth_step_size = initial_capacity;

    cpu_materials.reserve(initial_capacity);

    VkDeviceSize size = initial_capacity * sizeof(gfx::Material);

    bool render_initialized = core::BufferUtils::initialize_buffer(
        device, allocator, size, get_render_buffer());

    bool upload_initialized = core::BufferUtils::initialize_buffer(
        device, allocator, size, get_upload_buffer());

    return render_initialized && upload_initialized;
}

MaterialID
gfx::MaterialManager::create_material(const gfx::Material &material) {
    std::scoped_lock lock(material_mutex);
    // use a recycled value if available
    if (!recycle_cache.empty()) {
        MaterialID id = recycle_cache.top();
        recycle_cache.pop();
        if (id < cpu_materials.size()) {
            cpu_materials[id] = material;
            return id;
        }
        // If recycled ID is invalid (e.g. after resize), treat as new
    }

    // Add material to the vector, update count
    // and return the index as the material ID
    cpu_materials.push_back(material);
    material_count = static_cast<uint32_t>(cpu_materials.size());
    return MaterialID(material_count - 1);
}

void gfx::MaterialManager::remove_material(const MaterialID material_id) {
    std::scoped_lock lock(material_mutex);
    // add this to recycle_cache. the material cannot be
    // removed because this would break the indexing of
    // all existing materials.
    recycle_cache.push(material_id);
}

const gfx::Material &
gfx::MaterialManager::get_material(MaterialID material_id) const {
    if (material_id >= material_count)
        return gfx::Material{}; // TODO: handle this failure case
    else
        return cpu_materials[material_id];
}

void gfx::MaterialManager::update_material(MaterialID material_id,
                                           const gfx::Material &material) {
    std::scoped_lock lock(material_mutex);
    if (material_id < cpu_materials.size())
        cpu_materials[material_id] = material;
    // TODO: else log failure
}

void gfx::MaterialManager::update_buffers() {
    std::scoped_lock lock(material_mutex);

    // Resize if capacity exceeded
    if (material_count > max_materials[upload]) {
        uint32_t overflow = (material_count - max_materials[upload]);
        uint32_t chunks = 1 + (overflow / growth_step_size);
        uint32_t new_capacity =
            max_materials[upload] + (chunks * growth_step_size);
        resize_buffer(new_capacity);
    }

    // Upload CPU data to mapped GPU memory
    if (get_upload_buffer().mapped_data && !cpu_materials.empty()) {
        memcpy(get_upload_buffer().mapped_data, cpu_materials.data(),
               material_count * sizeof(Material));
    }
}

void gfx::MaterialManager::bind_descriptor(uint32_t binding_index) {
    std::shared_lock lock(material_mutex);
    core::BufferUtils::update_descriptor(
        device, get_render_buffer(), descriptor_set,
        material_count * sizeof(Material), binding_index);
}

void gfx::MaterialManager::shutdown() {
    std::scoped_lock lock(material_mutex);

    if (get_upload_buffer().allocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, get_upload_buffer().buffer,
                         get_upload_buffer().allocation);
        get_upload_buffer().buffer = VK_NULL_HANDLE;
        get_upload_buffer().allocation = VK_NULL_HANDLE;
        get_upload_buffer().mapped_data = nullptr;
    }

    if (get_render_buffer().allocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, get_render_buffer().buffer,
                         get_render_buffer().allocation);
        get_render_buffer().buffer = VK_NULL_HANDLE;
        get_render_buffer().allocation = VK_NULL_HANDLE;
        get_render_buffer().mapped_data = nullptr;
    }

    get_upload_buffer().info = {};
    get_upload_buffer().device_address = 0;

    get_render_buffer().info = {};
    get_render_buffer().device_address = 0;

    cpu_materials.clear();
    while (!recycle_cache.empty())
        recycle_cache.pop();

    material_count = 0;
    max_materials = {0, 0};

    device = VK_NULL_HANDLE;
    allocator = VK_NULL_HANDLE;
    descriptor_set = VK_NULL_HANDLE;
}

void gfx::MaterialManager::resize_buffer(uint32_t new_capacity) {

    // Must be called with material_mutex held
    VkDeviceSize new_size =
        static_cast<VkDeviceSize>(new_capacity * sizeof(Material));
    size_t data_size = material_count * sizeof(Material);

    core::BufferUtils::resize_buffer(device, allocator, new_size,
                                     get_upload_buffer(), cpu_materials.data(),
                                     data_size);
    max_materials[upload] = new_capacity;
}

void gfx::MaterialManager::toggle_buffers() {
    render ^= 1;
    upload = render ^ 1;
}

core::AllocatedBuffer &gfx::MaterialManager::get_upload_buffer() {
    return material_buffer[upload];
}

core::AllocatedBuffer &gfx::MaterialManager::get_render_buffer() {
    return material_buffer[render];
}
