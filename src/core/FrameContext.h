#pragma once

#include <vulkan/vulkan.h>

namespace core {
struct FrameContext {
    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
    VkSemaphore image_available_semaphore{VK_NULL_HANDLE};
    VkSemaphore render_finished_semaphore{VK_NULL_HANDLE};
    VkFence in_flight_fence{VK_NULL_HANDLE};
};
} // namespace core