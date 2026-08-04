#include "gfx/BufferUtils.h"
#include <cstring>

bool gfx::BufferUtils::initialize_buffer(
    VkDevice device, VmaAllocator allocator, VkDeviceSize size,
    gfx::AllocatedBuffer &allocated_buffer,
    VkBufferUsageFlags extraUsage) {

    // Create the initial GPU buffer
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                        extraUsage;
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

bool gfx::BufferUtils::resize_buffer(VkDevice device, VmaAllocator allocator,
                                      VkDeviceSize new_size,
                                      gfx::AllocatedBuffer &allocated_buffer,
                                      void *pData, size_t data_size,
                                      VkBufferUsageFlags extraUsage) {
    // Create new buffer
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = new_size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                        extraUsage;
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

bool gfx::BufferUtils::destroy_buffer(
    VkDevice device, VmaAllocator allocator,
    gfx::AllocatedBuffer &allocated_buffer) {
    if (allocated_buffer.allocation == VK_NULL_HANDLE)
        return false;

    vmaDestroyBuffer(allocator, allocated_buffer.buffer,
                     allocated_buffer.allocation);
    allocated_buffer.buffer = VK_NULL_HANDLE;
    allocated_buffer.allocation = VK_NULL_HANDLE;
    allocated_buffer.mapped_data = nullptr;
    allocated_buffer.info = {};
    allocated_buffer.device_address = 0;
    return true;
}

bool gfx::BufferUtils::update_descriptor(
    VkDevice device, const gfx::AllocatedBuffer &allocated_buffer,
    VkDescriptorSet descriptor_set, VkDeviceSize buffer_size,
    uint32_t binding_index) {
    // Delegate to the version with explicit type (defaults to STORAGE_BUFFER for compatibility)
    return update_descriptor(device, allocated_buffer, descriptor_set, buffer_size, binding_index,
                             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

bool gfx::BufferUtils::update_descriptor(
    VkDevice device, const gfx::AllocatedBuffer &allocated_buffer,
    VkDescriptorSet descriptor_set, VkDeviceSize buffer_size,
    uint32_t binding_index, VkDescriptorType descriptorType) {

    if (allocated_buffer.buffer == VK_NULL_HANDLE) {
        return false;
    }
    // VUID-VkDescriptorBufferInfo-range-00341: range must be > 0 if not WHOLE_SIZE.
    // Empty meshes (0 indices/verts before load finish) used to pass range=0.
    if (buffer_size == 0) {
        buffer_size = VK_WHOLE_SIZE;
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
    write.descriptorType = descriptorType;
    write.pBufferInfo = &buffer_info;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    return true;
}

bool gfx::BufferUtils::update_descriptor(
    VkDevice device, std::vector<VkDescriptorImageInfo> &image_infos,
    VkDescriptorSet descriptor_set, uint32_t binding_index) {
    
    VkDescriptorBufferInfo buffer_info;
    
    // If it's a vector of image infos, we need to write them to the descriptor set
    // However, vkUpdateDescriptorSets typically requires specific descriptor type handling.
    // For images, we use VkDescriptorImageInfo array.
    
    VkWriteDescriptorSet write_descriptor_set = {};
    write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write_descriptor_set.dstSet = descriptor_set;
    write_descriptor_set.dstBinding = binding_index;
    write_descriptor_set.dstArrayElement = 0;
    write_descriptor_set.descriptorCount = static_cast<uint32_t>(image_infos.size());
    write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write_descriptor_set.pImageInfo = image_infos.data();
    
    vkUpdateDescriptorSets(device, 1, &write_descriptor_set, 0, nullptr);
    
    return true;
}

void gfx::BufferUtils::transition_image_layout(
    VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout,
    VkImageLayout new_layout, uint32_t width, uint32_t height,
    uint32_t src_queue_index, uint32_t dst_queue_index) {

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.image = image;
    barrier.pNext = nullptr;

    // Source and destination queues family indices (VK_QUEUE_FAMILY_IGNORED if
    // not transferring ownership)
    barrier.srcQueueFamilyIndex = src_queue_index;
    barrier.dstQueueFamilyIndex = dst_queue_index;

    // Which part of the image to transition
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage, dst_stage;

    // When to execute this barrier (VK_PIPELINE_STAGE_TOP_OF_PIPE_WAIT before
    // execution)
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // Uploading to a texture for the first time
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // After uploading, transition to read-only for shaders
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;

    } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
               new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Direct transition (skipping transfer stage)
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;

    } else if (src_queue_index != dst_queue_index) { // Ownership transfer

        if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
            new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = 0; // release

            src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT; // release
            dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT; // acquire (dummy)
        } else {                                       // acquire to shader
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
    } else {
        return; // skip transition
    }

    vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
}
