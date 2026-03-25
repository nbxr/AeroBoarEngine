#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace core{
struct AllocatedImage
{
    VkImage           image       = VK_NULL_HANDLE;
    VmaAllocation     allocation  = VK_NULL_HANDLE;
    VmaAllocationInfo info{};
    VkImageView       view        = VK_NULL_HANDLE;   // often stored together
};};