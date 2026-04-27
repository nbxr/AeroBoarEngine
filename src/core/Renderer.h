#pragma once

#include "ComputePassContext.h"
#include "FrameContext.h"
#include "PassContext.h"
#include "PassType.h"
#include "SceneManager.h"
#include "VulkanContext.h"
#include "gfx/MaterialManager.h"
#include "gfx/MeshManager.h"
#include "gfx/TextureManager.h"
#include "vk_mem_alloc.h"
#include <GLFW/glfw3.h>

namespace core {
struct Renderer {
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    VmaAllocator allocator{};
    core::VulkanContext vk{};
    core::PassContext main_pass{};
    core::ComputePassContext compute[MAX_FRAMES_IN_FLIGHT]{};
    core::FrameContext frames[MAX_FRAMES_IN_FLIGHT]{};
    gfx::MaterialManager material_manager{};
    gfx::MeshManager mesh_manager{};
    gfx::TextureManager texture_manager{};
    core::SceneManager scene_manager{};

    struct Window {
        GLFWwindow *glfw_handle{nullptr};
        int32_t width{0};
        int32_t height{0};
    } window;
};
}; // namespace core
