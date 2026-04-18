#pragma once

#include "AllocatedBuffer.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>
#include "SceneInstance.h"

/**
 * @brief Manages the lifecycle and GPU buffers for scene instances.
 * Encapsulates VmaAllocation and VkBuffer via AllocatedBuffer.
 */
class SceneManager {
  public:
    SceneManager() = default;
    ~SceneManager();

    // Non-copyable
    SceneManager(const SceneManager &) = delete;
    SceneManager &operator=(const SceneManager &) = delete;

    /**
     * @brief Initialize SceneManager resources.
     * @param device Vulkan logical device.
     * @param allocator VMA allocator handle.
     * @param descriptor_set Bindless descriptor set containing
     * materials/meshes.
     * @param initial_capacity Initial capacity for scene instances.
     * @return true if initialization succeeded.
     */
    bool Initialize(VkDevice device, VmaAllocator allocator,
                    VkDescriptorSet descriptor_set, uint32_t initial_capacity);

    /**
     * @brief Add a new instance to the scene.
     * @param instance The SceneInstance data to add.
     * @return true if added successfully, false if capacity exceeded.
     */
    bool AddInstance(const SceneInstance &instance);

    /**
     * @brief Remove an instance by swapping it with the last element (O(1)).
     * @param index Index of the instance to remove.
     */
    void RemoveInstance(uint32_t index);

    /**
     * @brief Update GPU buffers with current CPU-side instance data.
     * Must be called before each frame if data has changed.
     */
    void UpdateBuffers();

    /**
     * @brief Bind the scene instance SSBO to the current command buffer.
     * @param command_buffer Vulkan command buffer.
     * @param binding_index Descriptor binding index.
     */
    void BindDescriptor(VkCommandBuffer command_buffer,
                        uint32_t binding_index) const;

    /**
     * @brief Get the number of active instances.
     */
    [[nodiscard]] uint32_t GetInstanceCount() const { return instance_count_; }

    /**
     * @brief Get the maximum capacity of the buffer.
     */
    [[nodiscard]] uint32_t GetMaxCapacity() const { return max_instances_; }

    /**
     * @brief Get pointer to CPU-accessible data for direct manipulation.
     * @return Pointer to the array of SceneInstance.
     */
    [[nodiscard]] const SceneInstance *GetInstances() const {
        return cpu_instances.data();
    }

    /**
     * @brief Get the underlying AllocatedBuffer for barrier operations etc.
     */
    [[nodiscard]] core::AllocatedBuffer &GetBuffer() {
        return instance_buffer;
    }

    /**
     * @brief Get the bindless descriptor set for materials/meshes.
     */
    [[nodiscard]] VkDescriptorSet GetDescriptorSet() const {
        return descriptor_set;
    }

    void Shutdown();

  private:
    void ResizeBuffer(uint32_t new_capacity);

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

    // Internal storage using AllocatedBuffer
    core::AllocatedBuffer instance_buffer;

    // CPU-side mirror for quick access/add/remove
    std::vector<SceneInstance> cpu_instances;

    uint32_t instance_count_ = 0;
    uint32_t max_instances_ = 0;
};
