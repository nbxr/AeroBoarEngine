#pragma once

#include "gfx/ComputePassContext.h"
#include "gfx/FrameContext.h"
#include "gfx/PassContext.h"
#include "gfx/PassType.h"
#include "gfx/VulkanContext.h"
#include "scene/SceneManager.h"
#include "gfx/MaterialManager.h"
#include "gfx/MeshManager.h"
#include "gfx/TextureManager.h"
#include "vk_mem_alloc.h"
#include <GLFW/glfw3.h>

namespace gfx {
struct Renderer {
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    VmaAllocator allocator{};
    VulkanContext vk{};
    PassContext main_pass{};
    ComputePassContext compute[MAX_FRAMES_IN_FLIGHT]{};
    FrameContext frames[MAX_FRAMES_IN_FLIGHT]{};
    MaterialManager material_manager{};
    MeshManager mesh_manager{};
    TextureManager texture_manager{};
    scene::SceneManager scene_manager{};

    uint32_t current_frame = 0;

    struct Window {
        GLFWwindow *glfw_handle{nullptr};
        int32_t width{0};
        int32_t height{0};
    } window;
};
} // namespace gfx
