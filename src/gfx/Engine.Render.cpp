#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include <vulkan/vulkan.h>
#include "gfx/VulkanContext.h"
#include "gfx/PassContext.h"
#include "gfx/Light.h"
#include "gfx/BufferUtils.h"
#include "gfx/PbrPush.h"
#include "core/Log.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>

void gfx::Engine::render() {
    auto& vk = renderer.vk;
    auto& frame = renderer.frames[renderer.current_frame];

    VkResult wait_res = vkWaitForFences(vk.device, 1, &frame.in_flight_fence, VK_TRUE, UINT64_MAX);
    if (wait_res == VK_ERROR_DEVICE_LOST) {
        renderer.vk.device_lost = true;
        LOG_ERROR("FATAL: vkWaitForFences returned VK_ERROR_DEVICE_LOST");
        return;
    } else if (wait_res != VK_SUCCESS) {
        LOG_ERROR("vkWaitForFences failed with VkResult=" << (int)wait_res);
        return;
    }

    // Previous GPU cull results for this frame slot are still in counts[] until
    // the next record() zeros them — log cull stats on change.
    // Skip the first read after load (fence starts signaled → counts still zero).
    if (renderer.gpu_culling.is_ready()) {
        const uint32_t visible =
            renderer.gpu_culling.read_visible_count(renderer.current_frame);
        const uint32_t total = renderer.last_total_render_meshes;
        static uint32_t frames_with_cull = 0;
        if (total > 0) {
            ++frames_with_cull;
            if (frames_with_cull > Renderer::MAX_FRAMES_IN_FLIGHT) {
                const uint32_t culled = (total > visible) ? (total - visible) : 0u;
                static uint32_t prev_culled = ~0u;
                static uint32_t prev_total = ~0u;
                const bool hzb =
                    renderer.last_cull_used_hzb[renderer.current_frame];
                static bool prev_hzb = false;
                if (culled != prev_culled || total != prev_total || hzb != prev_hzb) {
                    prev_culled = culled;
                    prev_total = total;
                    prev_hzb = hzb;
                    LOG_INFO("[Cull] " << culled << " of " << total
                             << " objects culled (" << visible << " drawn)"
                             << (hzb ? " [hzb=on]" : " [hzb=off]"));
                }
                renderer.last_visible_instances = visible;
            }
        }
    }

    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(
        vk.device,
        vk.swapchain,
        UINT64_MAX,
        vk.image_available_semaphores[renderer.current_frame],
        VK_NULL_HANDLE,
        &image_index
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        if (result == VK_ERROR_DEVICE_LOST) {
            renderer.vk.device_lost = true;
            LOG_ERROR("FATAL: vkAcquireNextImageKHR returned VK_ERROR_DEVICE_LOST");
        } else {
            LOG_ERROR("Failed to acquire swapchain image (VkResult=" << (int)result << ")");
        }
        return;
    }

    vkResetFences(vk.device, 1, &frame.in_flight_fence);
    vkResetCommandBuffer(frame.command_buffer, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(frame.command_buffer, &begin_info) != VK_SUCCESS) {
        LOG_ERROR("Failed to begin command buffer");
        return;
    }

    float aspect = (float)vk.swap_chain_extent.width / (float)vk.swap_chain_extent.height;
    glm::mat4 view = camera.get_view_matrix();
    glm::mat4 proj = camera.get_projection_matrix(aspect);
    proj[1][1] *= -1.0f;
    glm::mat4 viewProj = proj * view;

    // GPU frustum + previous-frame Hi-Z cull, then build indirect (before graphics).
    // HZB is gated with hysteresis: off on any camera motion, back on only after
    // the view has been still long enough (avoids mid-look occlusion flicker/pop).
    if (renderer.gpu_culling.is_ready()) {
        const uint32_t fi = renderer.current_frame;
        const glm::vec3 cam_pos = camera.get_position();
        const glm::vec3 cam_fwd = camera.get_forward();
        float hzb_bias_scale = 1.0f;
        if (renderer.hzb.should_use_occlusion(fi, cam_pos, cam_fwd, &hzb_bias_scale)) {
            const glm::mat4& hzb_vp = renderer.hzb.view_proj_for(fi);
            renderer.gpu_culling.record(
                frame.command_buffer, fi, viewProj, renderer.hzb.full_view(fi),
                renderer.hzb.sampler(), renderer.hzb.width(), renderer.hzb.height(),
                renderer.hzb.mip_count(), &hzb_vp, hzb_bias_scale);
            renderer.last_cull_used_hzb[fi] = true;
        } else {
            renderer.gpu_culling.record(frame.command_buffer, fi, viewProj);
            renderer.last_cull_used_hzb[fi] = false;
        }
    }

    write_frame_lighting(renderer.current_frame);

    // Ensure graphics set binding 1 points at this frame's instance buffer.
    if (renderer.gpu_culling.is_ready()) {
        auto& inst = renderer.gpu_culling.out_instances(renderer.current_frame);
        gfx::BufferUtils::update_descriptor(
            renderer.vk.device.device, inst,
            renderer.vk.bindless_descriptor_sets[renderer.current_frame],
            inst.info.size, Renderer::BINDING_DRAW_INSTANCES);
    }

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = renderer.main_pass.render_pass;
    render_pass_info.framebuffer = renderer.main_pass.framebuffers[image_index];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = vk.swap_chain_extent;

    std::array<VkClearValue, 4> clear_values{};
    if (renderer.main_pass.uses_depth_resolve) {
        clear_values[0].color = {{0.02f, 0.02f, 0.03f, 1.0f}}; // MSAA color
        clear_values[1].color = {{0.02f, 0.02f, 0.03f, 1.0f}}; // swapchain (unused)
        clear_values[2].depthStencil = {1.0f, 0};              // MSAA depth
        clear_values[3].depthStencil = {1.0f, 0};              // resolve depth (unused)
        render_pass_info.clearValueCount = 4;
    } else {
        clear_values[0].color = {{0.02f, 0.02f, 0.03f, 1.0f}};
        clear_values[1].depthStencil = {1.0f, 0};
        render_pass_info.clearValueCount = 2;
    }
    render_pass_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(frame.command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(frame.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline);

    vkCmdBindDescriptorSets(
        frame.command_buffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        vk.pipeline_layout,
        0, 1,
        &vk.bindless_descriptor_sets[renderer.current_frame],
        0, nullptr
    );

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

    auto& index_buf = renderer.mesh_manager.get_render_index_buffer();
    vkCmdBindIndexBuffer(frame.command_buffer, index_buf.buffer, 0, VK_INDEX_TYPE_UINT32);

    auto& vertex_buf = renderer.mesh_manager.get_render_vertex_buffer();
    VkDeviceSize vbo_offset = 0;
    vkCmdBindVertexBuffers(frame.command_buffer, 0, 1, &vertex_buf.buffer, &vbo_offset);

    gfx::PbrPush pushData{};
    pushData.viewProj = viewProj;
    pushData.extra = glm::uvec4{0u, 0u, 0u, 0u};

    if (renderer.gpu_culling.is_ready()) {
        const uint32_t batches = renderer.gpu_culling.batch_count();
        auto& indirect = renderer.gpu_culling.indirect_cmds(renderer.current_frame);

        // One push for the whole multi-draw; per-batch base is firstInstance
        // (folded into gl_InstanceIndex on Vulkan — see pbr.vert).
        vkCmdPushConstants(
            frame.command_buffer,
            vk.pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(PbrPush),
            &pushData);

        // multiDrawIndirect + drawIndirectFirstInstance (required at device select).
        // Stride 20 = sizeof(VkDrawIndexedIndirectCommand); empty batches have instanceCount=0.
        if (batches > 0) {
            vkCmdDrawIndexedIndirect(
                frame.command_buffer,
                indirect.buffer,
                0,
                batches,
                20);
        }
    }

    vkCmdEndRenderPass(frame.command_buffer);

    // Build previous-frame Hi-Z from resolved (or single-sample) depth.
    {
        VkImageView depth_view = VK_NULL_HANDLE;
        VkImage depth_image = VK_NULL_HANDLE;
        if (renderer.main_pass.uses_depth_resolve &&
            image_index < renderer.main_pass.resolved_depth_images.size()) {
            depth_view = renderer.main_pass.resolved_depth_images[image_index].view;
            depth_image = renderer.main_pass.resolved_depth_images[image_index].handle;
        } else if (image_index < renderer.main_pass.depth_images.size()) {
            depth_view = renderer.main_pass.depth_images[image_index].view;
            depth_image = renderer.main_pass.depth_images[image_index].handle;
        }
        if (depth_view != VK_NULL_HANDLE) {
            renderer.hzb.record_build(frame.command_buffer, renderer.current_frame,
                                      depth_view, depth_image,
                                      renderer.vk.swap_chain_extent, viewProj,
                                      camera.get_position(), camera.get_forward());
        }
    }

    if (vkEndCommandBuffer(frame.command_buffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to end command buffer");
        return;
    }

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = {vk.image_available_semaphores[renderer.current_frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &frame.command_buffer;

    VkSemaphore signal_semaphores[] = {vk.render_finished_semaphores[image_index]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    result = vkQueueSubmit(vk.graphics_queue, 1, &submit_info, frame.in_flight_fence);
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_DEVICE_LOST) {
            renderer.vk.device_lost = true;
            LOG_ERROR("FATAL: vkQueueSubmit returned VK_ERROR_DEVICE_LOST. GPU is gone.");
        } else {
            LOG_ERROR("vkQueueSubmit failed with VkResult=" << (int)result);
        }
        return;
    }

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;

    VkSwapchainKHR swapchains[] = {vk.swapchain};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;

    result = vkQueuePresentKHR(vk.present_queue, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
    } else if (result == VK_ERROR_DEVICE_LOST) {
        renderer.vk.device_lost = true;
        LOG_ERROR("FATAL: vkQueuePresentKHR returned VK_ERROR_DEVICE_LOST");
    } else if (result != VK_SUCCESS) {
        LOG_ERROR("vkQueuePresentKHR failed with VkResult=" << (int)result);
    }

    renderer.current_frame = (renderer.current_frame + 1) % Renderer::MAX_FRAMES_IN_FLIGHT;
}
