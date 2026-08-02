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

namespace gfx {
struct Renderer {
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    // Bindless set layout (textures must be highest binding for VARIABLE_COUNT):
    // 0 FrameConstants | 1 instances | 2 materials | 3 mesh meta
    // 4 verts | 5 indices | 6 lights SSBO
    // 7 prefiltered env cubemap | 8 BRDF LUT | 9 textures[]
    static constexpr uint32_t BINDING_FRAME_CONSTANTS = 0;
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

    // Engine-owned fallback / dev light when the scene has no KHR_lights_punctual.
    gfx::Light globalLight{};

    // Scene lights (world-space after load). Primary source for the lights SSBO.
    std::vector<gfx::Light> lights{};

    // Cached scene center for auto-exposure (updated at load).
    glm::vec3 scene_center{0.0f};

    // Per-frame FrameConstants UBO (binding 0) + lights SSBO (binding 6).
    // Double-buffered and paired with bindless_descriptor_sets[] by current_frame.
    std::array<AllocatedBuffer, 2> frame_constants_buffer{};
    std::array<AllocatedBuffer, 2> frame_lights_buffer{};

    // Engine IBL (procedural sky → SH + prefiltered cube + BRDF LUT)
    IblEnvironment ibl{};

    // Instanced draws: compact DrawInstanceGPU[] (binding 1) + mesh batches.
    // Built once at scene load; static scenes only for now.
    AllocatedBuffer draw_instance_buffer{};
    std::vector<DrawInstanceGPU> draw_instances_cpu{};
    std::vector<DrawBatch> draw_batches{};

    uint32_t current_frame = 0;

    struct Window {
        GLFWwindow *glfw_handle{nullptr};
        int32_t width{0};
        int32_t height{0};
    } window;
};
} // namespace gfx
