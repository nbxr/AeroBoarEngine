#pragma once

#include "Materials.h"
#include <memory>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace core {

class MaterialManager {
  public:
    MaterialManager();
    ~MaterialManager();

    // Initialize the material manager
    bool Initialize(VkDevice device, VmaAllocator allocator);

    // Create a new material and return its ID
    MaterialID CreateMaterial(const Material &material);

    // Get a material by its ID
    const Material &GetMaterial(MaterialID materialID) const;

    // Update a material by its ID
    void UpdateMaterial(MaterialID materialID, const Material &material);

    // Get the total number of materials
    uint32_t GetMaterialCount() const;

    // Upload all materials to GPU (creates SSBO)
    bool UploadMaterialsToGPU();

    // Get the GPU buffer containing materials
    VkBuffer GetMaterialBuffer() const;

    // Get the material buffer allocation
    VmaAllocation GetMaterialBufferAllocation() const;

    // Clear all materials
    void ClearMaterials();

    // Double buffering methods for dynamic material creation
    void BeginBackgroundUpload();
    bool IsBackgroundUploadComplete() const;
    void CompleteBackgroundUpload();

  private:
    VkDevice m_device{VK_NULL_HANDLE};
    VmaAllocator m_allocator{VK_NULL_HANDLE};

    // Material storage
    std::vector<Material> m_materials;
    std::vector<Material> m_pendingMaterials;

    // GPU buffer for materials
    VkBuffer m_materialBuffer = VK_NULL_HANDLE;
    VkBuffer m_pendingMaterialBuffer = VK_NULL_HANDLE;
    VmaAllocation m_materialBufferAllocation = VK_NULL_HANDLE;
    VmaAllocation m_pendingBufferAllocation = VK_NULL_HANDLE;
    VmaAllocationInfo m_materialBufferAllocationInfo = {};
    VmaAllocationInfo m_pendingBufferAllocationInfo = {};

    // Flag to track if materials have been uploaded to GPU
    bool m_isUploadedToGPU = false;
    bool m_isPendingUpload = false;
    VkFence m_uploadFence = VK_NULL_HANDLE;
};

} // namespace core