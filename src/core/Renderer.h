#pragma once

#include "ComputePassContext.h"
#include "PassContext.h"
#include "PassType.h"
#include "VulkanContext.h"
#include "vk_mem_alloc.h"
#include "MaterialManager.h"
#include <GLFW/glfw3.h>

namespace core {
struct Renderer {
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    
    VmaAllocator allocator{};
    core::VulkanContext vk{};
    core::PassContext pass{};
        // Per-frame in flight resources
    core::ComputePassContext computePasses[MAX_FRAMES_IN_FLIGHT];
    core::MaterialManager materialManager{};
    struct Window {
        GLFWwindow *glfw_handle{nullptr};
        int32_t width{};
        int32_t height{};
    } window;
};
}; // namespace core
