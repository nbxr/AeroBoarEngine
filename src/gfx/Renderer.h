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
#include "gfx/GpuCulling.h"
#include "gfx/HzbPyramid.h"
#include "gfx/DebugLinePass.h"
#include "gfx/HudTextPass.h"
#include "gfx/ShadowMap.h"
#include "gfx/TransparentPass.h"
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
    // 7 prefiltered env cubemap | 8 BRDF LUT | 9 joint matrices
    // 10 shadow map (sampler2DShadow) | 11 textures[]
    static constexpr uint32_t BINDING_FRAME_CONSTANTS = 0;
    static constexpr uint32_t BINDING_DRAW_INSTANCES = 1;
    static constexpr uint32_t BINDING_LIGHTS = 6;
    static constexpr uint32_t BINDING_IBL_SPECULAR = 7;
    static constexpr uint32_t BINDING_IBL_BRDF_LUT = 8;
    static constexpr uint32_t BINDING_JOINT_MATRICES = 9;
    static constexpr uint32_t BINDING_SHADOW = 10;
    static constexpr uint32_t BINDING_TEXTURES = 11;

    VmaAllocator allocator{};
    VulkanContext vk{};
    PassContext main_pass{};
    // Same-frame Hi-Z source: depth-only prepass (1x samples), one depth + FB per frame-in-flight.
    PassContext depth_prepass{};
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

    // Static mesh draw templates (built at load) + GPU frustum/occlusion cull.
    std::vector<MeshDrawInfo> mesh_draw_infos{};
    GpuCulling gpu_culling{};
    HzbPyramid hzb{};
    DebugLinePass debug_lines{};
    HudTextPass hud_text{};
    ShadowMap shadow_map{};
    TransparentPass transparent{};

    uint32_t last_visible_instances = 0;
    uint32_t last_total_render_meshes = 0;
    // Per frame-in-flight: whether that slot's last shade cull used Hi-Z (for [Cull] log).
    std::array<bool, MAX_FRAMES_IN_FLIGHT> last_cull_used_hzb{};
    // Bit i set → cull_items_[i] still needs model rewrite after a hierarchy change.
    uint32_t transform_upload_mask = 0;
    // Last TransformManager::world_serial() written into worlds_[i]. 0 = never.
    std::array<uint64_t, MAX_FRAMES_IN_FLIGHT> uploaded_world_serial{};

    uint32_t current_frame = 0;

    struct Window {
        GLFWwindow *glfw_handle{nullptr};
        int32_t width{0};
        int32_t height{0};
    } window;
};
} // namespace gfx
