#pragma once

#include "PassType.h"
#include "PassContext.h"
#include "VulkanContext.h"
#include "vk_mem_alloc.h"
#include <GLFW/glfw3.h>

namespace core {
struct Renderer {
    VulkanContext vk{};
    VmaAllocator allocator{};
    struct Window {
        GLFWwindow *glfw_handle{nullptr};
        int32_t width{};
        int32_t height{};
    } window;
    core::PassContext pass{};
};
}; // namespace core