#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace gfx {
struct AllocatedBuffer {
    VkBuffer buffer{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo info{};
    void *mapped_data = nullptr; // if persistently mapped
    VkDeviceAddress device_address{0}; // for shader device address / RTX / bindless
};
}; // namespace gfx