#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <mutex>

namespace aero_boar {

// Transfer manager for Vulkan transfer operations
class TransferManager {
public:
    TransferManager(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator);
    ~TransferManager();

    bool Initialize();
    void Shutdown();

    // Buffer operations
    bool CreateBuffer(VkBufferCreateInfo& bufferInfo, VmaAllocationCreateInfo& allocInfo,
                     VkBuffer& buffer, VmaAllocation& allocation);
    
    bool UploadBufferData(VkBuffer buffer, VmaAllocation allocation, 
                         const void* data, size_t dataSize);
    
    // Image operations
    bool CreateImage(VkImageCreateInfo& imageInfo, VmaAllocationCreateInfo& allocInfo,
                    VkImage& image, VmaAllocation& allocation);
    
    bool UploadImageData(VkImage image, VkImageCreateInfo& imageInfo,
                        const void* data, size_t dataSize);

    VkQueue GetTransferQueue() const { return m_transferQueue; }
    VkCommandPool GetCommandPool() const { return m_commandPool; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    std::mutex m_mutex;

    bool CreateTransferQueue();
    bool CreateCommandPool();
    bool CreateCommandBuffer();
    bool CreateFence();
    bool SubmitCommandBuffer();
    void WaitForCompletion();
};

} // namespace aero_boar

