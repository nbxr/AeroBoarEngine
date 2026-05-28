#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace gfx {
struct AllocatedImage {
    VkImage handle{VK_NULL_HANDLE};
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo info{};
    VkImageView view{VK_NULL_HANDLE}; // often stored together
};
}; // namespace gfx