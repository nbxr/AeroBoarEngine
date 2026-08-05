#include "gfx/MaterialManager.h"
#include "core/Log.h"
#include <mutex>
#include <shared_mutex>

namespace gfx {

MaterialManager::~MaterialManager() {
    if (is_initialized())
        shutdown();
}

bool MaterialManager::is_initialized() const {
    return device_ != VK_NULL_HANDLE && allocator_ != VK_NULL_HANDLE;
}

bool MaterialManager::initialize(VkDevice device, VmaAllocator allocator,
                                 uint32_t initial_capacity) {
    device_ = device;
    allocator_ = allocator;
    material_count_ = 0;
    if (initial_capacity > 0)
        growth_step_size_ = initial_capacity;

    cpu_materials_.reserve(initial_capacity);
    buffers_.set_max_elements_both(initial_capacity);

    const VkDeviceSize size =
        static_cast<VkDeviceSize>(initial_capacity) * sizeof(Material);
    return buffers_.initialize(device, allocator, size);
}

MaterialID MaterialManager::create_material(const Material& material) {
    std::scoped_lock lock(material_mutex_);
    if (!recycle_cache_.empty()) {
        const MaterialID id = recycle_cache_.top();
        recycle_cache_.pop();
        if (id < cpu_materials_.size()) {
            cpu_materials_[id] = material;
            return id;
        }
    }

    cpu_materials_.push_back(material);
    material_count_ = static_cast<uint32_t>(cpu_materials_.size());
    return MaterialID(material_count_ - 1);
}

void MaterialManager::remove_material(const MaterialID material_id) {
    std::scoped_lock lock(material_mutex_);
    recycle_cache_.push(material_id);
}

void MaterialManager::update_material(MaterialID material_id, const Material& material) {
    std::scoped_lock lock(material_mutex_);
    if (material_id < cpu_materials_.size()) {
        cpu_materials_[material_id] = material;
        return;
    }
    LOG_ERROR("[MaterialManager] update_material: invalid id " << material_id);
}

uint32_t MaterialManager::get_material_flags(uint32_t material_id) const {
    std::shared_lock lock(material_mutex_);
    if (material_id >= cpu_materials_.size())
        return 0;
    return cpu_materials_[material_id].flags;
}

const Material* MaterialManager::get_material(uint32_t material_id) const {
    std::shared_lock lock(material_mutex_);
    if (material_id >= cpu_materials_.size())
        return nullptr;
    return &cpu_materials_[material_id];
}

void MaterialManager::update_buffers() {
    std::scoped_lock lock(material_mutex_);

    buffers_.ensure_element_capacity(
        device_, allocator_, material_count_, sizeof(Material), growth_step_size_,
        cpu_materials_.empty() ? nullptr : cpu_materials_.data(),
        material_count_ * sizeof(Material));

    if (!cpu_materials_.empty()) {
        buffers_.upload_memcpy(cpu_materials_.data(),
                               material_count_ * sizeof(Material));
    }
}

void MaterialManager::bind_descriptor(uint32_t binding_index,
                                      VkDescriptorSet target_set) {
    std::shared_lock lock(material_mutex_);
    buffers_.bind_render_descriptor(device_, target_set, binding_index,
                                    material_count_ * sizeof(Material));
}

void MaterialManager::shutdown() {
    std::scoped_lock lock(material_mutex_);
    buffers_.destroy(device_, allocator_);
    cpu_materials_.clear();
    while (!recycle_cache_.empty())
        recycle_cache_.pop();
    material_count_ = 0;
    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

void MaterialManager::toggle_buffers() { buffers_.toggle(); }

} // namespace gfx
