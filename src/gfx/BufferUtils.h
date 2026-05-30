#pragma once
#include "gfx/AllocatedBuffer.h"
#include <vector>
#include <vma/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace gfx {
namespace BufferUtils {

bool initialize_buffer(VkDevice device, VmaAllocator allocator,
                       VkDeviceSize size,
                       AllocatedBuffer &allocated_buffer,
                       VkBufferUsageFlags extraUsage = 0);

bool resize_buffer(VkDevice device, VmaAllocator allocator,
                   VkDeviceSize new_size,
                   AllocatedBuffer &allocated_buffer, void *pData,
                   size_t data_size,
                   VkBufferUsageFlags extraUsage = 0);

bool destroy_buffer(VkDevice device, VmaAllocator allocator,
                    AllocatedBuffer &allocated_buffer);

bool update_descriptor(VkDevice device,
                       const AllocatedBuffer &allocated_buffer,
                       VkDescriptorSet descriptor_set, VkDeviceSize buffer_size,
                       uint32_t binding_index);

bool update_descriptor(VkDevice device,
                       std::vector<VkDescriptorImageInfo> &image_infos,
                       VkDescriptorSet descriptor_set, uint32_t binding_index);

void transition_image_layout(
    VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout,
    VkImageLayout new_layout, uint32_t width, uint32_t height,
    uint32_t src_queue_index = VK_QUEUE_FAMILY_IGNORED,
    uint32_t dst_queue_index = VK_QUEUE_FAMILY_IGNORED);

}; // namespace BufferUtils
} // namespace gfx