#include "core/transfer_manager.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace aero_boar {

TransferManager::TransferManager(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator)
    : m_device(device), m_physicalDevice(physicalDevice), m_allocator(allocator) {
}

TransferManager::~TransferManager() {
    Shutdown();
}

bool TransferManager::Initialize() {
    try {
        if (!CreateTransferQueue()) {
            std::cerr << "Failed to create transfer queue" << std::endl;
            return false;
        }

        if (!CreateCommandPool()) {
            std::cerr << "Failed to create command pool" << std::endl;
            return false;
        }

        if (!CreateCommandBuffer()) {
            std::cerr << "Failed to create command buffer" << std::endl;
            return false;
        }

        if (!CreateFence()) {
            std::cerr << "Failed to create fence" << std::endl;
            return false;
        }

        std::cout << "Transfer manager initialized successfully" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Transfer manager initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void TransferManager::Shutdown() {
    if (m_fence != VK_NULL_HANDLE) {
        vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(m_device, m_fence, nullptr);
        m_fence = VK_NULL_HANDLE;
    }

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    m_transferQueue = VK_NULL_HANDLE;
}

bool TransferManager::CreateTransferQueue() {
    // Find transfer queue family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t transferQueueFamilyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            transferQueueFamilyIndex = i;
            break;
        }
    }

    if (transferQueueFamilyIndex == UINT32_MAX) {
        // Fallback to graphics queue if no dedicated transfer queue
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                transferQueueFamilyIndex = i;
                break;
            }
        }
    }

    if (transferQueueFamilyIndex == UINT32_MAX) {
        std::cerr << "No suitable queue family found for transfer operations" << std::endl;
        return false;
    }

    vkGetDeviceQueue(m_device, transferQueueFamilyIndex, 0, &m_transferQueue);
    return true;
}

bool TransferManager::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    // Find the queue family index for transfer queue
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t transferQueueFamilyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
            transferQueueFamilyIndex = i;
            break;
        }
    }

    if (transferQueueFamilyIndex == UINT32_MAX) {
        // Fallback to graphics queue
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                transferQueueFamilyIndex = i;
                break;
            }
        }
    }

    poolInfo.queueFamilyIndex = transferQueueFamilyIndex;

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        std::cerr << "Failed to create transfer command pool" << std::endl;
        return false;
    }

    return true;
}

bool TransferManager::CreateCommandBuffer() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer) != VK_SUCCESS) {
        std::cerr << "Failed to allocate transfer command buffer" << std::endl;
        return false;
    }

    return true;
}

bool TransferManager::CreateFence() {
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0; // Not signaled initially

    if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_fence) != VK_SUCCESS) {
        std::cerr << "Failed to create transfer fence" << std::endl;
        return false;
    }

    return true;
}

bool TransferManager::CreateBuffer(VkBufferCreateInfo& bufferInfo, VmaAllocationCreateInfo& allocInfo,
                                  VkBuffer& buffer, VmaAllocation& allocation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    VkResult result = vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create buffer with VMA" << std::endl;
        return false;
    }

    return true;
}

bool TransferManager::UploadBufferData(VkBuffer buffer, VmaAllocation allocation, 
                                      const void* data, size_t dataSize) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Map memory and copy data
    void* mappedData;
    VkResult result = vmaMapMemory(m_allocator, allocation, &mappedData);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to map buffer memory" << std::endl;
        return false;
    }

    memcpy(mappedData, data, dataSize);
    vmaUnmapMemory(m_allocator, allocation);

    return true;
}

bool TransferManager::CreateImage(VkImageCreateInfo& imageInfo, VmaAllocationCreateInfo& allocInfo,
                                 VkImage& image, VmaAllocation& allocation) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    VkResult result = vmaCreateImage(m_allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create image with VMA" << std::endl;
        return false;
    }

    return true;
}

bool TransferManager::UploadImageData(VkImage image, VkImageCreateInfo& imageInfo,
                                     const void* data, size_t dataSize) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Reset command buffer
    vkResetCommandBuffer(m_commandBuffer, 0);

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS) {
        std::cerr << "Failed to begin command buffer for image upload" << std::endl;
        return false;
    }

    // Create staging buffer
    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = dataSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo = {};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VkResult result = vmaCreateBuffer(m_allocator, &stagingBufferInfo, &stagingAllocInfo, 
                                     &stagingBuffer, &stagingAllocation, nullptr);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create staging buffer" << std::endl;
        vkEndCommandBuffer(m_commandBuffer);
        return false;
    }

    // Copy data to staging buffer
    void* stagingData;
    result = vmaMapMemory(m_allocator, stagingAllocation, &stagingData);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to map staging buffer memory" << std::endl;
        vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAllocation);
        vkEndCommandBuffer(m_commandBuffer);
        return false;
    }

    memcpy(stagingData, data, dataSize);
    vmaUnmapMemory(m_allocator, stagingAllocation);

    // Transition image to transfer destination
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = imageInfo.mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = imageInfo.arrayLayers;

    vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {imageInfo.extent.width, imageInfo.extent.height, imageInfo.extent.depth};

    vkCmdCopyBufferToImage(m_commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition image to shader read optimal
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    // End command buffer
    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) {
        std::cerr << "Failed to end command buffer for image upload" << std::endl;
        vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    // Submit command buffer
    if (!SubmitCommandBuffer()) {
        vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    // Cleanup staging buffer
    vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAllocation);

    return true;
}

bool TransferManager::SubmitCommandBuffer() {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    if (vkQueueSubmit(m_transferQueue, 1, &submitInfo, m_fence) != VK_SUCCESS) {
        std::cerr << "Failed to submit transfer command buffer" << std::endl;
        return false;
    }

    WaitForCompletion();
    return true;
}

void TransferManager::WaitForCompletion() {
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_fence);
}

} // namespace aero_boar
