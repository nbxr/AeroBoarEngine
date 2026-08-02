#pragma once

#include <vulkan/vulkan.h>

namespace gfx {
struct FrameContext {
    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
    VkFence in_flight_fence{VK_NULL_HANDLE};
};
} // namespace gfx