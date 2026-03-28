#include "Engine.h"
#include "AllocatedBuffer.h"
#include "AllocatedImage.h"
#include "Renderer.h"

bool core::Engine::init_render_pass(core::Renderer &renderer) {
    // Multiview render pass for Quest 3
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = renderer.vk.swap_chain_image_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    if (vkCreateRenderPass(renderer.vk.device, &render_pass_info, nullptr,
                           &renderer.pass.render_pass) != VK_SUCCESS) {
        LOG_ERROR("Failed to create render pass");
        return false;
    }
    return true;
}

bool core::Engine::init_descriptor_pool(core::Renderer &renderer) {
    // Descriptor pool and set layout for bindless rendering
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000}};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 4;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = 1000;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

    if (vkCreateDescriptorPool(renderer.vk.device, &pool_info, nullptr,
                               &renderer.pass.descriptor_pool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor pool");
        return false;
    }
    return true;
}

bool core::Engine::init_descriptor_set_layout(core::Renderer &renderer) {
    VkDescriptorSetLayoutBinding bindings[4] = {};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 10000; // Large bindless array
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 4;
    layout_info.pBindings = bindings;
    layout_info.flags =
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

    if (vkCreateDescriptorSetLayout(renderer.vk.device, &layout_info, nullptr,
                                    &renderer.pass.descriptor_set_layout) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor set layout");
        return false;
    }
    return true;
}

bool core::Engine::init_command_pool(core::Renderer &renderer) {
    // Command pool
    VkCommandPoolCreateInfo cmd_pool_info = {};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = renderer.vk.graphics_family_index;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(renderer.vk.device, &cmd_pool_info, nullptr,
                            &renderer.vk.command_pool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create command pool");
        return false;
    }
    return true;
}

bool core::Engine::init_command_buffers(core::Renderer &renderer) {
    return false; // TODO: Implement init_framebuffers
}

bool core::Engine::init_framebuffers(core::Renderer &renderer) {
    // Framebuffers
    renderer.pass.framebuffers.resize(renderer.vk.swap_chain_images.size());
    for (size_t i = 0; i < renderer.vk.swap_chain_images.size(); i++) {
        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = renderer.pass.render_pass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = &renderer.vk.swap_chain_image_views[i];
        framebuffer_info.width = renderer.vk.swap_chain_extent.width;
        framebuffer_info.height = renderer.vk.swap_chain_extent.height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(renderer.vk.device, &framebuffer_info, nullptr,
                                &renderer.pass.framebuffers[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create framebuffer");
            return false;
        }
    }
    return true;
}

bool core::Engine::init_sync_primitives(core::Renderer &renderer) {
    // Synchronization primitives
    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < renderer.vk.swap_chain_images.size(); i++) {
        if (vkCreateSemaphore(renderer.vk.device, &semaphore_info, nullptr,
                              &renderer.vk.image_available_semaphores[i]) !=
                VK_SUCCESS ||
            vkCreateSemaphore(renderer.vk.device, &semaphore_info, nullptr,
                              &renderer.vk.render_finished_semaphores[i]) !=
                VK_SUCCESS ||
            vkCreateFence(renderer.vk.device, &fence_info, nullptr,
                          &renderer.vk.in_flight_fences[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create synchronization primitives");
            return false;
        }
    }

    return true;
}
