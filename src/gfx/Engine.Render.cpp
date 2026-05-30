#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include <vulkan/vulkan.h>
#include "gfx/VulkanContext.h"
#include "gfx/PassContext.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

void gfx::Engine::render() {
    auto& vk = renderer.vk;
    auto& frame = renderer.frames[renderer.current_frame];

    // Temporary debug: print first instance transform once
    static bool printed_first_transform = false;
    if (!printed_first_transform) {
        glm::mat4 t = renderer.scene_manager.get_debug_first_instance_transform();
        glm::vec3 pos = glm::vec3(t[3]);
        glm::vec3 scale(
            glm::length(glm::vec3(t[0])),
            glm::length(glm::vec3(t[1])),
            glm::length(glm::vec3(t[2]))
        );
        printf("[DEBUG] First instance position: (%.3f, %.3f, %.3f)  scale: (%.3f, %.3f, %.3f)\n",
               pos.x, pos.y, pos.z, scale.x, scale.y, scale.z);
        printed_first_transform = true;
    }

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
    // Bright magenta clear so it's obvious when we reach the render pass
    clear_values[0].color = {{1.0f, 0.0f, 1.0f, 1.0f}}; 
    clear_values[1].color = {{1.0f, 0.0f, 1.0f, 1.0f}};
    clear_values[2].depthStencil = {1.0f, 0};

    render_pass_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(frame.command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    // Bind pipeline (currently the screen_clear test pipeline)
    vkCmdBindPipeline(frame.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline);

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

    // === TEMP DEBUG (#1) ===
    // Force the camera to look at the first SceneInstance so we can see
    // the geometry with real per-instance transforms applied.
    // Remove this block once the camera system is smarter.
    {
        static bool firstFrame = true;
        if (firstFrame) {
            glm::mat4 t = renderer.scene_manager.get_debug_first_instance_transform();
            glm::vec3 target = glm::vec3(t[3]); // position from the matrix
            view = glm::lookAt(target + glm::vec3(0.0f, 2.0f, 5.0f), target, glm::vec3(0.0f, 1.0f, 0.0f));
            firstFrame = false;
        }
    }

    glm::mat4 viewProj = proj * view;

    vkCmdPushConstants(
        frame.command_buffer,
        vk.pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(glm::mat4),
        &viewProj
    );

    // === Proper indexed draw using uploaded mesh data ===
    const uint32_t vertex_offset = renderer.mesh_manager.get_debug_first_vertex_offset();
    const uint32_t index_offset  = renderer.mesh_manager.get_debug_first_index_offset();
    const uint32_t index_count   = renderer.mesh_manager.get_debug_first_index_count();

    // Bind the index buffer (contains all indices for the whole scene)
    auto& index_buf = renderer.mesh_manager.get_debug_render_index_buffer();
    vkCmdBindIndexBuffer(frame.command_buffer, index_buf.buffer, 0, VK_INDEX_TYPE_UINT32);

    // Draw the first mesh primitive properly (indexed)
    vkCmdDrawIndexed(frame.command_buffer, index_count, 1, index_offset, vertex_offset, 0);

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