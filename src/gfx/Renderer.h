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
#include "gfx/Light.h"
#include "gfx/AllocatedBuffer.h"
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

    // Engine-owned fallback / dev light. Used only when the loaded scene has
    // no KHR_lights_punctual lights. When a scene provides lights they become
    // the active source (world transforms applied at load).
    gfx::Light globalLight{};

    // Lights extracted from the glTF via KHR_lights_punctual (if any).
    // After load_scene these have been transformed into world space using
    // their node hierarchy and are the primary lighting data sent to FrameGlobals.
    std::vector<gfx::Light> lights{};

    // Per-frame globals UBO (binding 0) - camera + lights + exposure
    // Double-buffered for future dynamic updates
    std::array<AllocatedBuffer, 2> frame_globals_buffer{};
    uint32_t globals_upload = 1;
    uint32_t globals_render = 0;

    uint32_t current_frame = 0;

    struct Window {
        GLFWwindow *glfw_handle{nullptr};
        int32_t width{0};
        int32_t height{0};
    } window;
};
} // namespace gfx
