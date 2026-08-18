#include "scene/SceneManager.h"
#include "gfx/BufferUtils.h"
#include <mutex>

namespace scene {

SceneManager::~SceneManager() { shutdown(); }

bool SceneManager::is_initialized() const {
    return device_ != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE;
}

bool SceneManager::initialize(VkDevice device, VmaAllocator allocator,
                              uint32_t initial_capacity) {
    std::scoped_lock lock(instance_mutex_);
    device_ = device;
    allocator_ = allocator;
    instance_count_ = 0;
    if (initial_capacity > 0)
        growth_step_size_ = initial_capacity;

    cpu_instances_.reserve(initial_capacity);
    game_objects_.reserve(initial_capacity);
    render_meshes_.reserve(initial_capacity);
    instance_buffers_.set_max_elements_both(initial_capacity);

    const VkDeviceSize size =
        static_cast<VkDeviceSize>(initial_capacity) * sizeof(SceneInstance);
    return instance_buffers_.initialize(device, allocator, size);
}

uint32_t SceneManager::create_game_object(uint32_t root_transform_index,
                                          uint32_t gltf_node_index,
                                          uint32_t skin_index) {
    std::scoped_lock lock(instance_mutex_);
    if (!transforms_.is_alive(root_transform_index))
        return ~0u;

    GameObject go{};
    go.root_transform_index = root_transform_index;
    go.skin_index = skin_index;
    go.first_render_mesh = ~0u;
    go.render_mesh_count = 0;
    go.gltf_node_index = gltf_node_index;

    const uint32_t id = static_cast<uint32_t>(game_objects_.size());
    game_objects_.push_back(go);
    return id;
}

uint32_t SceneManager::add_render_mesh(uint32_t game_object_index, uint32_t mesh_index,
                                       uint32_t material_index,
                                       const core::AABB& local_aabb,
                                       uint32_t transform_index) {
    std::scoped_lock lock(instance_mutex_);
    if (game_object_index >= game_objects_.size())
        return ~0u;

    GameObject& go = game_objects_[game_object_index];
    const uint32_t xform =
        (transform_index != ~0u) ? transform_index : go.root_transform_index;

    RenderMesh rm{};
    rm.game_object_index = game_object_index;
    rm.mesh_index = mesh_index;
    rm.material_index = material_index;
    rm.transform_index = xform;
    rm.skin_index = go.skin_index;
    rm.local_aabb = local_aabb;
    rm.flags = 0;

    const uint32_t rm_id = static_cast<uint32_t>(render_meshes_.size());
    if (go.render_mesh_count == 0)
        go.first_render_mesh = rm_id;
    go.render_mesh_count++;
    render_meshes_.push_back(rm);

    // Dual-write legacy SceneInstance (world may be stale until propagate + refresh).
    SceneInstance inst{};
    inst.material_index = material_index;
    inst.mesh_index = mesh_index;
    inst.flags = 0;
    inst.transform = transforms_.get_world_matrix(xform);
    inst.local_aabb = local_aabb.transformed(inst.transform);
    cpu_instances_.push_back(inst);
    instance_count_ = static_cast<uint32_t>(cpu_instances_.size());

    return rm_id;
}

void SceneManager::refresh_instance_worlds() {
    std::scoped_lock lock(instance_mutex_);
    const size_t n = render_meshes_.size() < cpu_instances_.size()
                         ? render_meshes_.size()
                         : cpu_instances_.size();
    for (size_t i = 0; i < n; ++i) {
        const RenderMesh& rm = render_meshes_[i];
        SceneInstance& inst = cpu_instances_[i];
        const glm::mat4& world = transforms_.get_world_matrix(rm.transform_index);
        inst.transform = world;
        if (rm.local_aabb.is_valid())
            inst.local_aabb = rm.local_aabb.transformed(world);
    }
}

bool SceneManager::sync_transforms() {
    // Check outside the lock first for the common clean path.
    if (!transforms_.any_dirty())
        return false;

    std::scoped_lock lock(instance_mutex_);
    // Re-check after lock (another thread could have propagated — single-threaded
    // render loop today, but keep it correct).
    if (!transforms_.propagate())
        return false;
    // Shade/cull read TransformManager + GpuCulling worlds[], not SceneInstance.
    return true;
}

bool SceneManager::add_instance(const SceneInstance& instance) {
    std::scoped_lock lock(instance_mutex_);
    cpu_instances_.push_back(instance);
    instance_count_++;
    return true;
}

void SceneManager::remove_instance(uint32_t index) {
    if (index >= instance_count_)
        return;

    std::scoped_lock lock(instance_mutex_);
    const uint32_t last_index = instance_count_ - 1;
    if (index != last_index)
        cpu_instances_[index] = cpu_instances_[last_index];
    cpu_instances_.pop_back();
    instance_count_--;
}

void SceneManager::update_buffers() {
    instance_buffers_.ensure_element_capacity(
        device_, allocator_, instance_count_, sizeof(SceneInstance),
        growth_step_size_,
        cpu_instances_.empty() ? nullptr : cpu_instances_.data(),
        instance_count_ * sizeof(SceneInstance));

    if (!cpu_instances_.empty()) {
        instance_buffers_.upload_memcpy(cpu_instances_.data(),
                                        instance_count_ * sizeof(SceneInstance));
    }
}

void SceneManager::bind_descriptor(uint32_t binding_index,
                                   VkDescriptorSet target_set) {
    instance_buffers_.bind_render_descriptor(
        device_, target_set, binding_index,
        instance_count_ * sizeof(SceneInstance));
}

void SceneManager::shutdown() {
    clear_scene_data();

    if (allocator_ != VK_NULL_HANDLE)
        instance_buffers_.destroy(device_, allocator_);

    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

void SceneManager::clear_scene_data() {
    std::scoped_lock lock(instance_mutex_);
    transforms_.clear();
    animations_.clear();
    skins_.clear();
    morphs_.clear();
    gltf_node_to_transform_.clear();
    game_objects_.clear();
    render_meshes_.clear();
    cpu_instances_.clear();
    instance_count_ = 0;
}

void SceneManager::toggle_buffers() { instance_buffers_.toggle(); }

glm::mat4 SceneManager::get_first_instance_transform() const {
    std::shared_lock lock(instance_mutex_);
    if (!render_meshes_.empty())
        return transforms_.get_world_matrix(render_meshes_[0].transform_index);
    if (cpu_instances_.empty())
        return glm::mat4(1.0f);
    return cpu_instances_[0].transform;
}

std::pair<glm::vec3, float> SceneManager::get_first_instance_framing_sphere() const {
    std::shared_lock lock(instance_mutex_);
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
    if (cpu_instances_.empty())
        return {{0.0f, 0.0f, 0.0f}, 5.0f};
    const auto& inst = cpu_instances_[0];
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

std::pair<glm::vec3, float> SceneManager::get_scene_framing_sphere() const {
    std::shared_lock lock(instance_mutex_);

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
        for (const auto& inst : cpu_instances_) {
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
        for (const auto& inst : cpu_instances_) {
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

} // namespace scene
