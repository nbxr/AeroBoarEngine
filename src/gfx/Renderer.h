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
#include "gfx/IblEnvironment.h"
#include "gfx/DrawBatch.h"
#include "gfx/AllocatedBuffer.h"
#include "vk_mem_alloc.h"
#include <GLFW/glfw3.h>
#include <array>
#include <vector>
#include <vulkan/vulkan.h>

namespace gfx {
struct Renderer {
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // Bindless set layout (textures must be highest binding for VARIABLE_COUNT):
    // 0 FrameConstants | 1 draw instances | 2 materials | 3 mesh meta
    // 4 verts | 5 indices | 6 lights SSBO
    // 7 prefiltered env cubemap | 8 BRDF LUT | 9 textures[]
    static constexpr uint32_t BINDING_FRAME_CONSTANTS = 0;
    static constexpr uint32_t BINDING_DRAW_INSTANCES = 1;
    static constexpr uint32_t BINDING_LIGHTS = 6;
    static constexpr uint32_t BINDING_IBL_SPECULAR = 7;
    static constexpr uint32_t BINDING_IBL_BRDF_LUT = 8;
    static constexpr uint32_t BINDING_TEXTURES = 9;

    VmaAllocator allocator{};
    VulkanContext vk{};
    PassContext main_pass{};
    ComputePassContext compute[MAX_FRAMES_IN_FLIGHT]{};
    FrameContext frames[MAX_FRAMES_IN_FLIGHT]{};
    MaterialManager material_manager{};
    MeshManager mesh_manager{};
    TextureManager texture_manager{};
    scene::SceneManager scene_manager{};

    gfx::Light globalLight{};
    std::vector<gfx::Light> lights{};
    glm::vec3 scene_center{0.0f};

    std::array<AllocatedBuffer, 2> frame_constants_buffer{};
    std::array<AllocatedBuffer, 2> frame_lights_buffer{};

    IblEnvironment ibl{};

    // Static mesh draw templates (built at load). Per-frame cull fills instances + indirect.
    std::vector<MeshDrawInfo> mesh_draw_infos{};

    // Per-frame-in-flight: visible DrawInstanceGPU[] + draw commands after cull
    std::array<AllocatedBuffer, 2> draw_instance_buffer{};
    std::array<AllocatedBuffer, 2> indirect_draw_buffer{}; // VkDrawIndexedIndirectCommand
    // Base instance index into draw_instance_buffer for each indirect command
    // (must NOT also go in cmd.firstInstance — see prepare_culled_draws).
    std::array<std::vector<uint32_t>, 2> indirect_instance_bases{};
    std::array<uint32_t, 2> indirect_draw_count{}; // commands written this frame
    uint32_t max_draw_instances = 0;
    uint32_t max_indirect_draws = 0;

    // Optional: last frame cull stats for logging
    uint32_t last_visible_instances = 0;
    uint32_t last_total_render_meshes = 0;

    uint32_t current_frame = 0;

    struct Window {
        GLFWwindow *glfw_handle{nullptr};
        int32_t width{0};
        int32_t height{0};
    } window;
};
} // namespace gfx
