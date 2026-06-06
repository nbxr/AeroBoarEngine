#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include <vulkan/vulkan.h>
#include "gfx/VulkanContext.h"
#include "gfx/PassContext.h"
#include "scene/SceneInstance.h"
#include "gfx/Light.h"
#include "gfx/BufferUtils.h"
#include "gfx/PbrPush.h"

// GLM configuration for Vulkan
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

void gfx::Engine::render() {
    auto& vk = renderer.vk;
    auto& frame = renderer.frames[renderer.current_frame];

    // 1. Wait for the previous frame using this slot to finish
    vkWaitForFences(vk.device, 1, &frame.in_flight_fence, VK_TRUE, UINT64_MAX);

    // 2. Acquire next swapchain image
    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(
        vk.device,
        vk.swapchain,
        UINT64_MAX,
        vk.image_available_semaphores[renderer.current_frame],  // per-frame acquire semaphore
        VK_NULL_HANDLE,
        &image_index
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("Failed to acquire swapchain image");
        return;
    }

    // Reset fence for this frame
    vkResetFences(vk.device, 1, &frame.in_flight_fence);

    // 3. Record command buffer
    vkResetCommandBuffer(frame.command_buffer, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(frame.command_buffer, &begin_info) != VK_SUCCESS) {
        LOG_ERROR("Failed to begin command buffer");
        return;
    }

    // Begin render pass
    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = renderer.main_pass.render_pass;
    render_pass_info.framebuffer = renderer.main_pass.framebuffers[image_index];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = vk.swap_chain_extent;

    std::array<VkClearValue, 3> clear_values{};
    // Bright magenta so it's obvious if we're hitting the render pass
    clear_values[0].color = {{1.0f, 0.0f, 1.0f, 1.0f}};
    clear_values[1].color = {{1.0f, 0.0f, 1.0f, 1.0f}};
    clear_values[2].depthStencil = {1.0f, 0};

    render_pass_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(frame.command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    // Bind pipeline (currently the screen_clear test pipeline)
    vkCmdBindPipeline(frame.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline);

    // Phase 2: update per-frame globals UBO (camera + scene lights or fallback)
    // MUST happen BEFORE vkCmdBindDescriptorSets so that the bind for the
    // bindless set (which includes binding 0 for FrameGlobals) sees the
    // current descriptor pointing at a buffer whose mapped contents have the
    // correct light data (especially lightParams[i].x type) for this frame.
    // The draws later in this command buffer will then read the right lightType
    // in pbr.frag.
    {
        uint32_t renderIdx = renderer.globals_render;
        auto* dst = static_cast<gfx::FrameGlobals*>(renderer.frame_globals_buffer[renderIdx].mapped_data);
        if (dst) {
            // Zero the entire mapped FrameGlobals first. This is required for two
            // reasons:
            // 1. std140 array-of-scalar padding rules mean there are "holes" (the
            //    padding0/1 regions as seen by the shader) that must read as 0.
            //    Direct field writes below do not touch every byte.
            // 2. When falling back to globalLight we only write slot [0]; higher
            //    slots must be zero so they don't leak stale values when lightCount
            //    is small (the previous "index 6" symptom was a layout mismatch
            //    variant of this + the padding stride issue).
            memset(dst, 0, sizeof(gfx::FrameGlobals));

            dst->cameraPosition = glm::vec4(camera.get_position(), 1.0f);

            if (!renderer.lights.empty()) {
                // Authoritative scene lights from the glTF (point light in this case).
                // We write them here every frame directly into the side that will
                // be referenced by the descriptor we are about to bind.
                const uint32_t n = std::min<uint32_t>(renderer.lights.size(), gfx::MAX_LIGHTS);
                dst->lightCount = n;
                float maxI = 0.0f;
                for (uint32_t i = 0; i < gfx::MAX_LIGHTS; ++i) {
                    if (i < n) {
                        const auto &L = renderer.lights[i];
                        dst->lightDirectionsOrPositions[i] =
                            glm::vec4(L.positionOrDirection, 0.0f);
                        dst->lightColors[i] = glm::vec4(L.color, L.intensity);
                        dst->lightParams[i] =
                            glm::vec4(static_cast<float>(L.type), L.range,
                                      L.innerConeAngle, L.outerConeAngle);
                        if (L.intensity > maxI) maxI = L.intensity;
                    } else {
                        // Explicitly zero unused slots so stale data from prior
                        // frames / larger light counts / other writes cannot
                        // appear at higher indices (e.g. [6]) while lightCount=1.
                        dst->lightDirectionsOrPositions[i] = glm::vec4(0.0f);
                        dst->lightColors[i] = glm::vec4(0.0f);
                        dst->lightParams[i] = glm::vec4(0.0f);
                    }
                }
                dst->exposure = (maxI > 10.0f) ? (20.0f / maxI) : 1.0f;
            } else if (renderer.globalLight.type == gfx::LightType::Directional) {
                dst->lightCount = 1;
                dst->exposure = 1.0f;
                dst->lightDirectionsOrPositions[0] = glm::vec4(renderer.globalLight.positionOrDirection, 0.0f);
                dst->lightColors[0] = glm::vec4(renderer.globalLight.color, renderer.globalLight.intensity);
                dst->lightParams[0] = glm::vec4(
                    static_cast<float>(renderer.globalLight.type),
                    renderer.globalLight.range,
                    renderer.globalLight.innerConeAngle,
                    renderer.globalLight.outerConeAngle);
            }
        }

        gfx::BufferUtils::update_descriptor(
            renderer.vk.device.device,
            renderer.frame_globals_buffer[renderIdx],
            renderer.vk.bindless_descriptor_set,
            sizeof(gfx::FrameGlobals),
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    }

    // Bind the global bindless descriptor set (we will use it soon for real draws)
    vkCmdBindDescriptorSets(
        frame.command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        vk.pipeline_layout,
        0, 1,
        &vk.bindless_descriptor_set,
        0, nullptr
    );

    // Dynamic viewport + scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(vk.swap_chain_extent.width);
    viewport.height = static_cast<float>(vk.swap_chain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frame.command_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = vk.swap_chain_extent;
    vkCmdSetScissor(frame.command_buffer, 0, 1, &scissor);

    // Use the real camera system (now that we've validated the vertex buffer)
    float aspect = (float)vk.swap_chain_extent.width / (float)vk.swap_chain_extent.height;
    glm::mat4 view = camera.get_view_matrix();
    glm::mat4 proj = camera.get_projection_matrix(aspect);
    proj[1][1] *= -1.0f; // Vulkan clip space flip

    glm::mat4 viewProj = proj * view;

    // Push constant struct matching pbr.vert / pbr.frag layout
    // (viewProj + model + uvec4 extra + cameraPos for Phase 1 lighting)
    gfx::PbrPush pushData{};

    // (per-frame globals update for camera + lights moved earlier, before the
    // bindless descriptor set bind, so the draw calls see the correct lightType
    // from the glTF point light.)

    auto& index_buf = renderer.mesh_manager.get_render_index_buffer();
    vkCmdBindIndexBuffer(frame.command_buffer, index_buf.buffer, 0, VK_INDEX_TYPE_UINT32);

    auto& vertex_buf = renderer.mesh_manager.get_render_vertex_buffer();
    VkDeviceSize vbo_offset = 0;
    vkCmdBindVertexBuffers(frame.command_buffer, 0, 1, &vertex_buf.buffer, &vbo_offset);

    // Draw ALL meshes/primitives in the scene (one draw call per SceneInstance primitive)
    const uint32_t inst_count = renderer.scene_manager.get_instance_count();
    for (uint32_t i = 0; i < inst_count; ++i) {
        const scene::SceneInstance &inst = renderer.scene_manager.get_instance(i);
        const uint32_t prim = inst.mesh_index;

        const uint32_t vtx_off = renderer.mesh_manager.get_primitive_vertex_offset(prim);
        const uint32_t idx_off = renderer.mesh_manager.get_primitive_index_offset(prim);
        const uint32_t idx_cnt = renderer.mesh_manager.get_primitive_index_count(prim);

        if (idx_cnt == 0)
            continue;

        // Hierarchical transforms are fully resolved on the CPU during glTF loading
        // (see add_mesh_node in Engine.InitializeScene.cpp). The world matrix stored
        // in SceneInstance.transform is the final composed transform for that primitive.
        // It is sent directly as the model matrix in the push constant.
        // The vertex shader simply does: worldPos = model * localPos
        // There is no additional hierarchy or skinning transform in the shader at this time.
        pushData.viewProj = viewProj;
        pushData.model = inst.transform;
        pushData.cameraPos = glm::vec4(camera.get_position(), 1.0f); // Phase 1 lighting: real view vector

        // === DEBUG MODE (optional) ===
        // Set debugMode to non-zero values for diagnostics:
        //   0 = Normal textured rendering (default)
        //   1 = UV visualization (Red = U, Green = V)
        //   2+ = Per-primitive color (each draw call gets its own color)
        uint debugMode = 0;
        uint primitiveHint = uint(i % 32);

        pushData.extra = glm::uvec4{inst.material_index, debugMode + primitiveHint, 0, 0};

        vkCmdPushConstants(
            frame.command_buffer,
            vk.pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(PbrPush),
            &pushData
        );

        vkCmdDrawIndexed(frame.command_buffer, idx_cnt, 1, idx_off, vtx_off, 0);
    }

    vkCmdEndRenderPass(frame.command_buffer);

    if (vkEndCommandBuffer(frame.command_buffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to end command buffer");
        return;
    }

    // 4. Submit
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = {vk.image_available_semaphores[renderer.current_frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &frame.command_buffer;

    // Use render finished semaphore indexed by the actual image we acquired
    VkSemaphore signal_semaphores[] = {vk.render_finished_semaphores[image_index]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    if (vkQueueSubmit(vk.graphics_queue, 1, &submit_info, frame.in_flight_fence) != VK_SUCCESS) {
        LOG_ERROR("Failed to submit draw command buffer");
        return;
    }

    // 5. Present
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;  // the per-image render_finished semaphore

    VkSwapchainKHR swapchains[] = {vk.swapchain};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;

    result = vkQueuePresentKHR(vk.present_queue, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
    } else if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to present swapchain image");
    }

    // Advance to next frame in flight
    renderer.current_frame = (renderer.current_frame + 1) % Renderer::MAX_FRAMES_IN_FLIGHT;
}