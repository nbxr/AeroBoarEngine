#pragma once
#include "AllocatedBuffer.h"
#include <vma/vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace core {
namespace BufferUtils {

bool initialize_buffer(VkDevice device, VmaAllocator allocator,
                       VkDeviceSize size,
                       core::AllocatedBuffer &allocated_buffer);

bool resize_buffer(VkDevice device, VmaAllocator allocator,
                   VkDeviceSize new_size,
                   core::AllocatedBuffer &allocated_buffer, void *pData,
                   size_t data_size);

bool destroy_buffer(VkDevice device, VmaAllocator allocator,
                    core::AllocatedBuffer &allocated_buffer);

bool update_descriptor(VkDevice device,
                       const core::AllocatedBuffer &allocated_buffer,
                       VkDescriptorSet descriptor_set, VkDeviceSize buffer_size,
                       uint32_t binding_index);

}; // namespace BufferUtils
}; // namespace core