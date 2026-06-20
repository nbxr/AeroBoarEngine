#include "scene/SceneManager.h"
#include "gfx/BufferUtils.h"
#include <cstring>

scene::SceneManager::~SceneManager() { shutdown(); }

bool scene::SceneManager::is_initialized() {
    return device != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE;
}

bool scene::SceneManager::initialize(VkDevice device, VmaAllocator allocator,
                                    uint32_t initial_capacity) {
    std::scoped_lock lock(instance_mutex);
    this->device = device;
    this->allocator = allocator;
    this->instance_count = 0;
    for (size_t i = 0; i < max_instances.size(); i++)
        this->max_instances[i] = initial_capacity;
    if (initial_capacity > 0)
        this->growth_step_size = initial_capacity;

    cpu_instances.reserve(initial_capacity);

    VkDeviceSize size = initial_capacity * sizeof(SceneInstance);
    bool render_initialized = gfx::BufferUtils::initialize_buffer(
        device, allocator, size, get_render_buffer());
    bool upload_initialized = gfx::BufferUtils::initialize_buffer(
        device, allocator, size, get_upload_buffer());

    return render_initialized && upload_initialized;
}

bool scene::SceneManager::add_instance(const SceneInstance &instance) {
    std::scoped_lock lock(instance_mutex);

    cpu_instances.push_back(instance);
    instance_count++;

    return true;
}

void scene::SceneManager::remove_instance(uint32_t index) {
    if (index >= instance_count) {
        return;
    }

    std::scoped_lock lock(instance_mutex);

    // Swap with last element for O(1) removal
    uint32_t last_index = instance_count - 1;
    if (index != last_index) {
        cpu_instances[index] = cpu_instances[last_index];
    }

    cpu_instances.pop_back();
    instance_count--;
}

void scene::SceneManager::update_buffers() {
    // Resize if capacity exceeded
    if (instance_count > max_instances[upload]) {
        uint32_t overflow = (instance_count - max_instances[upload]);
        uint32_t chunks = 1 + (overflow / growth_step_size);
        uint32_t new_capacity =
            max_instances[upload] + (chunks * growth_step_size);
        resize_buffer(new_capacity);
    }

    // Upload CPU data to mapped GPU memory
    if (get_upload_buffer().mapped_data && !cpu_instances.empty()) {
        memcpy(get_upload_buffer().mapped_data, cpu_instances.data(),
               instance_count * sizeof(SceneInstance));
    }
}

void scene::SceneManager::bind_descriptor(uint32_t binding_index, VkDescriptorSet target_set) {
    VkDeviceSize size = instance_count * sizeof(SceneInstance);
    gfx::BufferUtils::update_descriptor(device, get_render_buffer(),
                                         target_set, size, binding_index);
}

void scene::SceneManager::shutdown() {

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

    device = VK_NULL_HANDLE;
    allocator = VK_NULL_HANDLE;
    cpu_instances.clear();
    instance_count = 0;
    max_instances = {0, 0};
}

void scene::SceneManager::resize_buffer(uint32_t new_capacity) {
    VkDeviceSize new_size = new_capacity * sizeof(SceneInstance);

    size_t data_size = cpu_instances.size() * sizeof(SceneInstance);
    gfx::BufferUtils::resize_buffer(device, allocator, new_size,
                                     get_upload_buffer(), cpu_instances.data(),
                                     data_size);
    max_instances[upload] = new_capacity;
}

void scene::SceneManager::toggle_buffers() {
    render ^= 1;
    upload = render ^ 1;
}

gfx::AllocatedBuffer &scene::SceneManager::get_upload_buffer() {
    return instance_buffer[upload];
}

gfx::AllocatedBuffer &scene::SceneManager::get_render_buffer() {
    return instance_buffer[render];
}

glm::mat4 scene::SceneManager::get_first_instance_transform() const {
    std::shared_lock lock(instance_mutex);
    if (cpu_instances.empty()) {
        return glm::mat4(1.0f);
    }
    return cpu_instances[0].transform;
}

std::pair<glm::vec3, float> scene::SceneManager::get_first_instance_framing_sphere() const {
    std::shared_lock lock(instance_mutex);
    if (cpu_instances.empty()) {
        return {{0.0f, 0.0f, 0.0f}, 5.0f};
    }

    const auto& inst = cpu_instances[0];

    if (inst.local_aabb.is_valid()) {
        glm::vec3 c = inst.local_aabb.center();
        glm::vec3 ext = inst.local_aabb.extents();
        float radius = glm::length(ext) * 0.5f;   // bounding sphere
        if (radius < 0.01f) radius = 1.0f;

        // Small upward bias (proportional to object size) for more pleasing views
        // (historical value was a fixed +1.2f offset for the helmet)
        c.y += 0.12f * radius;

        return {c, radius};
    }

    // Fallback for legacy cases with no AABB data
    glm::vec3 c = glm::vec3(inst.transform[3]);
    return {c, 7.0f};
}
