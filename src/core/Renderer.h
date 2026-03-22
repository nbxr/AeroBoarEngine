#pragma once

#include "vk_mem_alloc.h"
#include <GLFW/glfw3.h>
#include "VulkanContext.h"

namespace Core {
struct Renderer {
  VulkanContext vk{};
  VmaAllocator allocator{};
  struct Window {
    GLFWwindow *glfw_handle{nullptr};
    int32_t width{};
    int32_t height{};
  } window;
};
}; // namespace Core