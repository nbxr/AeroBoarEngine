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
    game_objects_.reserve(initial_capacity);
    render_meshes_.reserve(initial_capacity);

    VkDeviceSize size = initial_capacity * sizeof(SceneInstance);
    bool render_initialized = gfx::BufferUtils::initialize_buffer(
        device, allocator, size, get_render_buffer());
    bool upload_initialized = gfx::BufferUtils::initialize_buffer(
        device, allocator, size, get_upload_buffer());

    return render_initialized && upload_initialized;
}

uint32_t scene::SceneManager::create_game_object(uint32_t root_transform_index,
                                                 uint32_t gltf_node_index) {
    std::scoped_lock lock(instance_mutex);
    if (!transforms_.is_alive(root_transform_index)) {
        return ~0u;
    }

    GameObject go{};
    go.root_transform_index = root_transform_index;
    go.first_render_mesh = ~0u;
    go.render_mesh_count = 0;
    go.gltf_node_index = gltf_node_index;

    uint32_t id = static_cast<uint32_t>(game_objects_.size());
    game_objects_.push_back(go);
    return id;
}

uint32_t scene::SceneManager::add_render_mesh(uint32_t game_object_index,
                                              uint32_t mesh_index,
                                              uint32_t material_index,
                                              const core::AABB& local_aabb,
                                              uint32_t transform_index) {
    std::scoped_lock lock(instance_mutex);
    if (game_object_index >= game_objects_.size())
        return ~0u;

    GameObject& go = game_objects_[game_object_index];
    uint32_t xform =
        (transform_index != ~0u) ? transform_index : go.root_transform_index;

    RenderMesh rm{};
    rm.game_object_index = game_object_index;
    rm.mesh_index = mesh_index;
    rm.material_index = material_index;
    rm.transform_index = xform;
    rm.skin_index = go.skin_index;
    rm.local_aabb = local_aabb;
    rm.flags = 0;

    uint32_t rm_id = static_cast<uint32_t>(render_meshes_.size());
    if (go.render_mesh_count == 0)
        go.first_render_mesh = rm_id;
    go.render_mesh_count++;
    render_meshes_.push_back(rm);

    // Dual-write legacy SceneInstance (world AABB) so older helpers still work.
    // World may be stale until propagate() + refresh_instance_worlds().
    SceneInstance inst{};
    inst.material_index = material_index;
    inst.mesh_index = mesh_index;
    inst.flags = 0;
    inst.transform = transforms_.get_world_matrix(xform);
    inst.local_aabb = local_aabb.transformed(inst.transform);
    cpu_instances.push_back(inst);
    instance_count = static_cast<uint32_t>(cpu_instances.size());

    return rm_id;
}

void scene::SceneManager::refresh_instance_worlds() {
    std::scoped_lock lock(instance_mutex);
    const size_t n = render_meshes_.size() < cpu_instances.size()
                         ? render_meshes_.size()
                         : cpu_instances.size();
    for (size_t i = 0; i < n; ++i) {
        const RenderMesh& rm = render_meshes_[i];
        SceneInstance& inst = cpu_instances[i];
        const glm::mat4& world = transforms_.get_world_matrix(rm.transform_index);
        inst.transform = world;
        if (rm.local_aabb.is_valid())
            inst.local_aabb = rm.local_aabb.transformed(world);
    }
}

bool scene::SceneManager::add_instance(const SceneInstance& instance) {
    std::scoped_lock lock(instance_mutex);
    cpu_instances.push_back(instance);
    instance_count++;
    return true;
}

void scene::SceneManager::remove_instance(uint32_t index) {
    if (index >= instance_count)
        return;

    std::scoped_lock lock(instance_mutex);
    uint32_t last_index = instance_count - 1;
    if (index != last_index)
        cpu_instances[index] = cpu_instances[last_index];
    cpu_instances.pop_back();
    instance_count--;
}

void scene::SceneManager::update_buffers() {
    if (instance_count > max_instances[upload]) {
        uint32_t overflow = (instance_count - max_instances[upload]);
        uint32_t chunks = 1 + (overflow / growth_step_size);
        uint32_t new_capacity =
            max_instances[upload] + (chunks * growth_step_size);
        resize_buffer(new_capacity);
    }

    if (get_upload_buffer().mapped_data && !cpu_instances.empty()) {
        memcpy(get_upload_buffer().mapped_data, cpu_instances.data(),
               instance_count * sizeof(SceneInstance));
    }
}

void scene::SceneManager::bind_descriptor(uint32_t binding_index,
                                          VkDescriptorSet target_set) {
    VkDeviceSize size = instance_count * sizeof(SceneInstance);
    gfx::BufferUtils::update_descriptor(device, get_render_buffer(), target_set,
                                        size, binding_index);
}

void scene::SceneManager::shutdown() {
    clear_scene_data();

    // Idempotent: safe if already shut down or never initialized.
    if (allocator != VK_NULL_HANDLE) {
        if (get_upload_buffer().allocation != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, get_upload_buffer().buffer,
                             get_upload_buffer().allocation);
            get_upload_buffer() = {};
        }
        if (get_render_buffer().allocation != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, get_render_buffer().buffer,
                             get_render_buffer().allocation);
            get_render_buffer() = {};
        }
    }

    max_instances = {0, 0};
    device = VK_NULL_HANDLE;
    allocator = VK_NULL_HANDLE;
}

void scene::SceneManager::clear_scene_data() {
    std::scoped_lock lock(instance_mutex);
    transforms_.clear();
    game_objects_.clear();
    render_meshes_.clear();
    cpu_instances.clear();
    instance_count = 0;
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

gfx::AllocatedBuffer& scene::SceneManager::get_upload_buffer() {
    return instance_buffer[upload];
}

gfx::AllocatedBuffer& scene::SceneManager::get_render_buffer() {
    return instance_buffer[render];
}

glm::mat4 scene::SceneManager::get_first_instance_transform() const {
    std::shared_lock lock(instance_mutex);
    if (!render_meshes_.empty()) {
        return transforms_.get_world_matrix(render_meshes_[0].transform_index);
    }
    if (cpu_instances.empty())
        return glm::mat4(1.0f);
    return cpu_instances[0].transform;
}

std::pair<glm::vec3, float>
scene::SceneManager::get_first_instance_framing_sphere() const {
    std::shared_lock lock(instance_mutex);
    if (!render_meshes_.empty()) {
        const auto& rm = render_meshes_[0];
        const glm::mat4& w = transforms_.get_world_matrix(rm.transform_index);
        core::AABB world = rm.local_aabb.transformed(w);
        if (world.is_valid()) {
            glm::vec3 c = world.center();
            float radius = glm::length(world.extents()) * 0.5f;
            if (radius < 0.01f)
                radius = 1.0f;
            c.y += 0.12f * radius;
            return {c, radius};
        }
    }
    if (cpu_instances.empty())
        return {{0.0f, 0.0f, 0.0f}, 5.0f};
    const auto& inst = cpu_instances[0];
    if (inst.local_aabb.is_valid()) {
        glm::vec3 c = inst.local_aabb.center();
        float radius = glm::length(inst.local_aabb.extents()) * 0.5f;
        if (radius < 0.01f)
            radius = 1.0f;
        c.y += 0.12f * radius;
        return {c, radius};
    }
    return {glm::vec3(inst.transform[3]), 7.0f};
}

std::pair<glm::vec3, float> scene::SceneManager::get_scene_framing_sphere() const {
    std::shared_lock lock(instance_mutex);

    core::AABB scene_bounds;
    bool any = false;

    if (!render_meshes_.empty()) {
        for (const auto& rm : render_meshes_) {
            if (!rm.local_aabb.is_valid())
                continue;
            const glm::mat4& w = transforms_.get_world_matrix(rm.transform_index);
            core::AABB world = rm.local_aabb.transformed(w);
            if (!world.is_valid())
                continue;
            scene_bounds.expand(world.min);
            scene_bounds.expand(world.max);
            any = true;
        }
    } else {
        for (const auto& inst : cpu_instances) {
            if (!inst.local_aabb.is_valid())
                continue;
            scene_bounds.expand(inst.local_aabb.min);
            scene_bounds.expand(inst.local_aabb.max);
            any = true;
        }
    }

    if (!any) {
        for (const auto& rm : render_meshes_) {
            scene_bounds.expand(
                glm::vec3(transforms_.get_world_matrix(rm.transform_index)[3]));
            any = true;
        }
        for (const auto& inst : cpu_instances) {
            scene_bounds.expand(glm::vec3(inst.transform[3]));
            any = true;
        }
        if (!any)
            return {{0.0f, 0.0f, 0.0f}, 5.0f};
    }

    glm::vec3 c = scene_bounds.center();
    float radius = glm::length(scene_bounds.extents()) * 0.5f;
    if (radius < 0.01f)
        radius = 1.0f;
    c.y += 0.12f * radius;
    return {c, radius};
}
