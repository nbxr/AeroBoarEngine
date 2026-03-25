#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace core {
struct AllocatedBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo info{};   // optional: size, offset, memory handle, etc.
    void *mappedData = nullptr; // if persistently mapped
    VkDeviceAddress deviceAddress =
        0; // for shader device address / RTX / bindless
};
}; // namespace core