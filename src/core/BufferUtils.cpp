#include "BufferUtils.h"
#include <cstring>

bool core::BufferUtils::initialize_buffer(
    VkDevice device, VmaAllocator allocator, VkDeviceSize size,
    core::AllocatedBuffer &allocated_buffer) {

    // Create the initial GPU buffer
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // VMA: mapped + sequential host access
    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkResult result = vmaCreateBuffer(
        allocator, &buffer_info, &alloc_info, &allocated_buffer.buffer,
        &allocated_buffer.allocation, &allocated_buffer.info);

    if (result != VK_SUCCESS) {
        return false;
    }

    // Get device address for bindless
    VkBufferDeviceAddressInfo buffer_addr = {};
    buffer_addr.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    buffer_addr.buffer = allocated_buffer.buffer;
    allocated_buffer.device_address =
        vkGetBufferDeviceAddress(device, &buffer_addr);

    // vmaCreateBuffer with MAPPED flag already maps the memory
    allocated_buffer.mapped_data = allocated_buffer.info.pMappedData;

    // Clear the buffer
    memset(allocated_buffer.mapped_data, 0, buffer_info.size);

    return true;
}

bool core::BufferUtils::resize_buffer(VkDevice device, VmaAllocator allocator,
                                      VkDeviceSize new_size,
                                      core::AllocatedBuffer &allocated_buffer,
                                      void *pData, size_t data_size) {
    // Create new buffer
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = new_size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // VMA: mapped + sequential host access
    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer new_buffer;
    VmaAllocation new_allocation;
    VmaAllocationInfo new_alloc_info;

    VkResult result =
        vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &new_buffer,
                        &new_allocation, &new_alloc_info);
    if (result != VK_SUCCESS) {
        return false;
    }

    // Copy old CPU data to new buffer's mapped memory
    if (data_size > 0) {
        memcpy(new_alloc_info.pMappedData, pData, data_size);
    }

    // Destroy old buffer
    vmaDestroyBuffer(allocator, allocated_buffer.buffer,
                     allocated_buffer.allocation);

    // Update allocated buffer
    allocated_buffer.buffer = new_buffer;
    allocated_buffer.allocation = new_allocation;
    allocated_buffer.info = new_alloc_info;
    allocated_buffer.mapped_data = new_alloc_info.pMappedData;

    // Get device address
    VkBufferDeviceAddressInfo buffer_addr = {};
    buffer_addr.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    buffer_addr.buffer = new_buffer;
    allocated_buffer.device_address =
        vkGetBufferDeviceAddress(device, &buffer_addr);

    return true;
}

bool core::BufferUtils::update_descriptor(
    VkDevice device, const core::AllocatedBuffer &allocated_buffer,
    VkDescriptorSet descriptor_set, VkDeviceSize buffer_size,
    uint32_t binding_index) {

    if (allocated_buffer.buffer == VK_NULL_HANDLE) {
        return false;
    }

    // Update the descriptor set with the current buffer
    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = allocated_buffer.buffer;
    buffer_info.offset = 0;
    buffer_info.range = buffer_size;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set;
    write.dstBinding = binding_index;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffer_info;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    return true;
}
