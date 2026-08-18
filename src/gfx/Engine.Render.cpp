#include "gfx/Engine.h"
#include "gfx/Depth.h"
#include "gfx/Renderer.h"
#include "core/Frustum.h"
#include <vulkan/vulkan.h>
#include "gfx/VulkanContext.h"
#include "gfx/PassContext.h"
#include "gfx/Light.h"
#include "gfx/PbrPush.h"
#include "core/Log.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <vector>

namespace {

void record_indirect_draws(VkCommandBuffer cmd, gfx::Renderer& renderer,
                           VkPipeline pipeline, const glm::mat4& viewProj,
                           gfx::CullPass pass = gfx::CullPass::Opaque) {
    auto& vk = renderer.vk;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 0, 1,
                            &vk.bindless_descriptor_sets[renderer.current_frame], 0, nullptr);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(vk.swap_chain_extent.width);
    viewport.height = static_cast<float>(vk.swap_chain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = vk.swap_chain_extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    auto& index_buf = renderer.mesh_manager.get_render_index_buffer();
    vkCmdBindIndexBuffer(cmd, index_buf.buffer, 0, VK_INDEX_TYPE_UINT32);

    auto& vertex_buf = renderer.mesh_manager.get_render_vertex_buffer();
    VkDeviceSize vbo_offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buf.buffer, &vbo_offset);

    gfx::PbrPush pushData{};
    pushData.viewProj = viewProj;
    pushData.extra = glm::uvec4{0u, 0u, 0u, 0u};

    vkCmdPushConstants(cmd, vk.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(gfx::PbrPush), &pushData);

    const uint32_t batches = renderer.gpu_culling.batch_count();
    auto& indirect = renderer.gpu_culling.indirect_cmds(renderer.current_frame, pass);
    if (batches > 0) {
        vkCmdDrawIndexedIndirect(cmd, indirect.buffer, 0, batches,
                                 sizeof(VkDrawIndexedIndirectCommand));
    }
}

} // namespace

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

    // After this frame slot's fence wait: safe to rewrite its GPU cull models.
    // Dirty hierarchy → propagate worlds, dual-write instances, update cull items.
    sync_scene_transforms();

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

    const uint32_t fi = renderer.current_frame;
    const bool can_cull = renderer.gpu_culling.is_ready();

    const bool have_transparents =
        can_cull && renderer.gpu_culling.transparent_item_count() > 0 &&
        renderer.gpu_culling.has_transparent_half();
    const bool gpu_transparent =
        have_transparents && renderer.transparent.wboit_ready();
    const bool cpu_transparent = have_transparents && !gpu_transparent;
    if (cpu_transparent) {
        const core::Frustum fr = core::Frustum::from_view_proj(viewProj);
        renderer.transparent.collect_and_sort(
            renderer.scene_manager, renderer.material_manager,
            renderer.mesh_manager, fr, camera.get_position());
        renderer.transparent.upload_instances(renderer.gpu_culling, fi);
    } else {
        renderer.transparent.clear_items();
    }
    const bool can_hzb = occlusion_cull_enabled_ && can_cull &&
                         renderer.hzb.is_ready() &&
                         fi < renderer.depth_prepass.framebuffers.size() &&
                         fi < renderer.depth_prepass.depth_images.size() &&
                         renderer.vk.depth_prepass_pipeline != VK_NULL_HANDLE;

    // ------------------------------------------------------------------
    // Compute culls stay outside render passes (Adreno: no mid-RP barriers).
    //   [hzb] frustum opaque → prepass → HZB → frustum+HZB opaque [+ transparent]
    //   [default] one frustum opaque cull; WBOIT transparents are a second emit
    // Instance SSBO is bound at build_scene — not UPDATE_AFTER_BIND here.
    // ------------------------------------------------------------------
    write_frame_lighting(renderer.current_frame);

    auto record_transparent_cull = [&](bool hzb) {
        if (!gpu_transparent)
            return;
        if (hzb) {
            renderer.gpu_culling.record(frame.command_buffer, fi, viewProj, true,
                                        renderer.hzb.width(), renderer.hzb.height(),
                                        renderer.hzb.mip_count(), 0.003f,
                                        gfx::CullEmitFilter::Transparent);
        } else {
            renderer.gpu_culling.record(frame.command_buffer, fi, viewProj, false, 0,
                                        0, 0, 0.003f, gfx::CullEmitFilter::Transparent);
        }
    };

    if (can_hzb) {
        renderer.gpu_culling.record(frame.command_buffer, fi, viewProj, false, 0, 0,
                                    0, 0.003f, gfx::CullEmitFilter::OpaqueDepth);

        VkClearValue clear_depth{};
        clear_depth.depthStencil = {gfx::kDepthClear, 0};

        VkRenderPassBeginInfo prepass_info{};
        prepass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        prepass_info.renderPass = renderer.depth_prepass.render_pass;
        prepass_info.framebuffer = renderer.depth_prepass.framebuffers[fi];
        prepass_info.renderArea.offset = {0, 0};
        prepass_info.renderArea.extent = vk.swap_chain_extent;
        prepass_info.clearValueCount = 1;
        prepass_info.pClearValues = &clear_depth;

        vkCmdBeginRenderPass(frame.command_buffer, &prepass_info,
                             VK_SUBPASS_CONTENTS_INLINE);
        record_indirect_draws(frame.command_buffer, renderer,
                              vk.depth_prepass_pipeline, viewProj,
                              gfx::CullPass::Opaque);
        vkCmdEndRenderPass(frame.command_buffer);

        renderer.hzb.record_build(frame.command_buffer, fi, vk.swap_chain_extent);
        renderer.gpu_culling.record(frame.command_buffer, fi, viewProj, true,
                                    renderer.hzb.width(), renderer.hzb.height(),
                                    renderer.hzb.mip_count(), 0.003f,
                                    gfx::CullEmitFilter::OpaqueDepth);
        record_transparent_cull(true);
        renderer.last_cull_used_hzb[fi] = true;
    } else {
        if (can_cull) {
            renderer.gpu_culling.record(frame.command_buffer, fi, viewProj, false, 0,
                                        0, 0, 0.003f, gfx::CullEmitFilter::OpaqueDepth);
            record_transparent_cull(false);
        }
        renderer.last_cull_used_hzb[fi] = false;
    }

    // 5) Main shade: **opaque first** (depth write on), then **transparent**
    //    (depth test on, depth write off) so glass never blocks the cabin.
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
        clear_values[2].depthStencil = {gfx::kDepthClear, 0};  // MSAA depth
        clear_values[3].depthStencil = {gfx::kDepthClear, 0};  // resolve depth (unused)
        render_pass_info.clearValueCount = 4;
    } else {
        clear_values[0].color = {{0.02f, 0.02f, 0.03f, 1.0f}};
        clear_values[1].depthStencil = {gfx::kDepthClear, 0};
        render_pass_info.clearValueCount = 2;
    }
    render_pass_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(frame.command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    if (can_cull) {
        // Graphics only — no compute/barriers inside the render pass.
        // Opaque + transparent share one instance SSBO (different firstInstance
        // bases); do not re-update binding 1 between these draws.
        record_indirect_draws(frame.command_buffer, renderer, vk.pipeline, viewProj,
                              gfx::CullPass::Opaque);

        // WBOIT path draws after this RP. Fallback: sorted traditional blend here.
        if (cpu_transparent && vk.transparent_pipeline != VK_NULL_HANDLE) {
            const uint32_t base = renderer.gpu_culling.instance_slot_count();
            renderer.transparent.record_sorted_draws(
                frame.command_buffer, renderer, vk.transparent_pipeline, viewProj,
                base);
        }
    }

    // Physics collider wireframes (after shade so they sit on top with depth test).
    if (physics.is_debug_draw_enabled() && renderer.debug_lines.is_ready()) {
        std::vector<physics::DebugVertex> lines;
        physics.collect_debug_lines(lines, camera.get_position());
        if (!lines.empty()) {
            renderer.debug_lines.draw(frame.command_buffer, fi,
                                      vk.swap_chain_extent, viewProj, lines);
        }
    }

    vkCmdEndRenderPass(frame.command_buffer);

    bool presented_by_wboit = false;
    if (gpu_transparent ||
        (renderer.transparent.wboit_ready() && renderer.transparent.count() > 0)) {
        renderer.transparent.record_wboit(frame.command_buffer, renderer, fi,
                                          image_index, viewProj, gpu_transparent);
        presented_by_wboit = true;
    }
    if (!presented_by_wboit && image_index < vk.swap_chain_images.size()) {
        VkImageMemoryBarrier to_present{};
        to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_present.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        to_present.dstAccessMask = 0;
        to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.image = vk.swap_chain_images[image_index];
        to_present.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(frame.command_buffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &to_present);
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
