#include "MaterialManager.h"
#include <cstring>
#include <stdexcept>

namespace core {
MaterialManager::MaterialManager() {}

MaterialManager::~MaterialManager() {
    ClearMaterials();

    // Clean up GPU buffer if it exists
    if (m_materialBuffer != VK_NULL_HANDLE &&
        m_materialBufferAllocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_materialBuffer,
                         m_materialBufferAllocation);
        m_materialBuffer = VK_NULL_HANDLE;
        m_materialBufferAllocation = VK_NULL_HANDLE;
    }
}

bool MaterialManager::Initialize(VkDevice device, VmaAllocator allocator) {
    m_device = device;
    m_allocator = allocator;
    // Initialize with empty materials vector
    m_materials.clear();
    m_isUploadedToGPU = false;

    // Initialize pending buffer
    if (m_pendingMaterialBuffer == VK_NULL_HANDLE) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(Material) * 1000; // Maximum expected
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkResult result = vmaCreateBuffer(
            m_allocator, &bufferInfo, &allocInfo, &m_pendingMaterialBuffer,
            &m_pendingBufferAllocation, &m_pendingBufferAllocationInfo);
        if (result != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to create pending material buffer");
        }
    }

    return true;
}

MaterialID MaterialManager::CreateMaterial(const Material &material) {
    // Add material to the vector
    m_materials.push_back(material);

    // Return the index as the material ID
    return static_cast<MaterialID>(m_materials.size() - 1);
}

const Material &MaterialManager::GetMaterial(MaterialID materialID) const {
    if (materialID >= m_materials.size()) {
        throw std::out_of_range("Material ID out of range");
    }
    return m_materials[materialID];
}

void MaterialManager::UpdateMaterial(MaterialID materialID,
                                     const Material &material) {
    if (materialID >= m_materials.size()) {
        throw std::out_of_range("Material ID out of range");
    }
    m_materials[materialID] = material;
}

uint32_t MaterialManager::GetMaterialCount() const {
    return static_cast<uint32_t>(m_materials.size());
}

bool MaterialManager::UploadMaterialsToGPU() {
    if (m_materials.empty()) {
        m_isUploadedToGPU = true;
        return true;
    }

    // If already uploaded, destroy the old buffer
    if (m_materialBuffer != VK_NULL_HANDLE &&
        m_materialBufferAllocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_materialBuffer,
                         m_materialBufferAllocation);
        m_materialBuffer = VK_NULL_HANDLE;
        m_materialBufferAllocation = VK_NULL_HANDLE;
    }

    // Create buffer description
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(Material) * m_materials.size();
    bufferInfo.usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Allocation info
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    // Create the buffer
    VkResult result = vmaCreateBuffer(
        m_allocator, &bufferInfo, &allocInfo, &m_materialBuffer,
        &m_materialBufferAllocation, &m_materialBufferAllocationInfo);

    if (result != VK_SUCCESS) {
        return false;
    }

    // Copy data to GPU buffer
    void *mappedData;
    result = vmaMapMemory(m_allocator, m_materialBufferAllocation, &mappedData);
    if (result != VK_SUCCESS) {
        return false;
    }

    std::memcpy(mappedData, m_materials.data(),
                sizeof(Material) * m_materials.size());

    vmaUnmapMemory(m_allocator, m_materialBufferAllocation);

    m_isUploadedToGPU = true;
    return true;
}

VkBuffer MaterialManager::GetMaterialBuffer() const { return m_materialBuffer; }

VmaAllocation MaterialManager::GetMaterialBufferAllocation() const {
    return m_materialBufferAllocation;
}

void MaterialManager::ClearMaterials() {
    m_materials.clear();
    m_isUploadedToGPU = false;

    // Clean up GPU buffer if it exists
    if (m_materialBuffer != VK_NULL_HANDLE &&
        m_materialBufferAllocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_materialBuffer,
                         m_materialBufferAllocation);
        m_materialBuffer = VK_NULL_HANDLE;
        m_materialBufferAllocation = VK_NULL_HANDLE;
    }
}

// Double buffering implementation
void MaterialManager::BeginBackgroundUpload() {
    // Start a fence to track upload completion
    if (m_uploadFence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device, m_uploadFence, nullptr);
    }
    vkCreateFence(m_device, 0, nullptr, &m_uploadFence);

    // Create pending buffer if not already created
    if (m_pendingMaterialBuffer == VK_NULL_HANDLE) {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(Material) * m_pendingMaterials.size();
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

        VkResult result = vmaCreateBuffer(
            m_allocator, &bufferInfo, &allocInfo, &m_pendingMaterialBuffer,
            &m_pendingBufferAllocation, &m_pendingBufferAllocationInfo);
        if (result != VK_SUCCESS) {
            throw std::runtime_error(
                "Failed to create pending material buffer");
        }
    }

    // Mark upload as in progress
    m_isPendingUpload = true;
}

bool MaterialManager::IsBackgroundUploadComplete() const {
    return m_uploadFence != VK_NULL_HANDLE &&
           vkGetFenceStatus(m_device, m_uploadFence) == VK_SUCCESS;
}

void MaterialManager::CompleteBackgroundUpload() {
    // Destroy the fence
    if (m_uploadFence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device, m_uploadFence, nullptr);
        m_uploadFence = VK_NULL_HANDLE;
    }

    // Swap buffers
    std::swap(m_materials, m_pendingMaterials);
    std::swap(m_materialBuffer, m_pendingMaterialBuffer);
    std::swap(m_materialBufferAllocation, m_pendingBufferAllocation);
    std::swap(m_materialBufferAllocationInfo, m_pendingBufferAllocationInfo);

    // Mark upload as complete
    m_isPendingUpload = false;

    // Clean up old buffer
    if (m_materialBuffer != VK_NULL_HANDLE &&
        m_materialBufferAllocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_materialBuffer,
                         m_materialBufferAllocation);
        m_materialBuffer = VK_NULL_HANDLE;
        m_materialBufferAllocation = VK_NULL_HANDLE;
    }

    // Clean up old allocation info
    m_materialBufferAllocationInfo = {};
    m_materialBufferAllocationInfo = {};
}
}; // namespace core
