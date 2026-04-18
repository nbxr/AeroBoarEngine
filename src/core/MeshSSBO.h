#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include "AllocatedBuffer.h"
#include "MeshData.h"

namespace core {
/**
 * @brief Manages the GPU SSBO for mesh data.
 * Follows the "Bindless" strategy by storing all mesh data in a single buffer.
 */
class MeshSSBO {
public:
    MeshSSBO() = default;
    ~MeshSSBO();

    // Non-copyable
    MeshSSBO(const MeshSSBO&) = delete;
    MeshSSBO& operator=(const MeshSSBO&) = delete;

    /**
     * @brief Initialize the SSBO buffer.
     * @param device Vulkan logical device.
     * @param allocator VMA allocator handle.
     * @param initial_capacity Initial capacity for mesh entries.
     * @return true if initialization succeeded.
     */
    bool Initialize(VkDevice device, VmaAllocator allocator, uint32_t initial_capacity);

    /**
     * @brief Add a new mesh entry.
     * @param mesh The mesh data to add.
     * @return true if added successfully.
     */
    bool AddMesh(const core::MeshData& mesh);

    /**
     * @brief Update GPU buffer with current CPU-side mesh data.
     */
    void UpdateBuffers();

    /**
     * @brief Bind the mesh SSBO to the current command buffer.
     * @param command_buffer Vulkan command buffer.
     * @param binding_index Descriptor binding index.
     */
    void BindDescriptor(VkCommandBuffer command_buffer, uint32_t binding_index) const;

    /**
     * @brief Get the number of active meshes.
     */
    [[nodiscard]] uint32_t GetMeshCount() const { return mesh_count; }

    /**
     * @brief Get the underlying AllocatedBuffer for barrier operations etc.
     */
    [[nodiscard]] core::AllocatedBuffer& GetBuffer() { return mesh_buffer; }

    /**
     * @brief Get the bindless descriptor set for mesh data.
     */
    [[nodiscard]] VkDescriptorSet GetDescriptorSet() const { return descriptor_set; }

    void Shutdown();

private:
    void ResizeBuffer(uint32_t new_capacity);

    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

    // Internal storage using AllocatedBuffer
    core::AllocatedBuffer mesh_buffer;

    // CPU-side mirror for quick access
    std::vector<MeshData> cpu_meshes;
    
    uint32_t mesh_count = 0;
    uint32_t max_meshes = 0;
};
}; // namespace core