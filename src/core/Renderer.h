#pragma once

#include "PassContext.h"
#include "PassType.h"
#include "VulkanContext.h"
#include "vk_mem_alloc.h"
#include <GLFW/glfw3.h>

namespace core {
struct Renderer {
    VmaAllocator allocator{};
    core::VulkanContext vk{};
    core::PassContext pass{};
    struct Window {
        GLFWwindow *glfw_handle{nullptr};
        int32_t width{};
        int32_t height{};
    } window;
};
}; // namespace core