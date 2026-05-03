#include "Engine.h"
#include "AllocatedBuffer.h"
#include "AllocatedImage.h"
#include "Renderer.h"

bool core::Engine::init_render_pass() {
    // Multiview render pass for Quest 3
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = renderer.vk.swap_chain_image_format;
    color_attachment.samples = renderer.vk.msaa_color;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.format = renderer.vk.depth_format;
    depth_attachment.samples = renderer.vk.msaa_depth;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp =
        VK_ATTACHMENT_STORE_OP_DONT_CARE; // Optimization: Don't write back to
                                          // DRAM
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription swapchain_attachment = {};
    swapchain_attachment.format = renderer.vk.swap_chain_image_format;
    swapchain_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    swapchain_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    swapchain_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    swapchain_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    swapchain_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    swapchain_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapchain_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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

    // The three attachments must match the order used in
    // VkFramebuffers
    VkAttachmentDescription attachments[3] = {
        color_attachment,     // 0: Transient MSAA Color
        swapchain_attachment, // 1: Resolved Color (swapchain image)
        depth_attachment      // 2: Transient Depth
    };

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 3;
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    if (vkCreateRenderPass(renderer.vk.device, &render_pass_info, nullptr,
                           &renderer.main_pass.render_pass) != VK_SUCCESS) {
        LOG_ERROR("Failed to create render pass");
        return false;
    }
    return true;
}

bool core::Engine::init_msaa_color_image() {

    // Create transient MSAA color image (on-chip only, no DRAM writes)
    VkImageCreateInfo color_image_info = {};
    color_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    color_image_info.imageType = VK_IMAGE_TYPE_2D;
    color_image_info.format = renderer.vk.swap_chain_image_format;
    color_image_info.extent = {renderer.vk.swap_chain_extent.width,
                               renderer.vk.swap_chain_extent.height, 1};
    color_image_info.mipLevels = 1;
    color_image_info.arrayLayers = 1;
    color_image_info.samples = renderer.vk.msaa_color; // 4x MSAA for Quest 3
    color_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    color_image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    color_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    color_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(renderer.allocator, &color_image_info, &alloc_info,
                       &renderer.main_pass.msaa_color_image.handle,
                       &renderer.main_pass.msaa_color_image.allocation,
                       &renderer.main_pass.msaa_color_image.info) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create MSAA color image");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = renderer.main_pass.msaa_color_image.handle;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = renderer.vk.swap_chain_image_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(renderer.vk.device, &view_info, nullptr,
                          &renderer.main_pass.msaa_color_image.view) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create MSAA color image view");
        return false;
    }

    return true;
}

bool core::Engine::init_depth_image() {

    // Create transient depth image (no DRAM writes, DONT_CARE store)
    VkImageCreateInfo depth_image_info = {};
    depth_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depth_image_info.imageType = VK_IMAGE_TYPE_2D;
    depth_image_info.format = renderer.vk.depth_format;
    depth_image_info.extent = {renderer.vk.swap_chain_extent.width,
                               renderer.vk.swap_chain_extent.height, 1};
    depth_image_info.mipLevels = 1;
    depth_image_info.arrayLayers = 1;
    depth_image_info.samples =
        renderer.vk.msaa_color; // Match MSAA sample count
    depth_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    depth_image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    depth_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depth_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(renderer.allocator, &depth_image_info, &alloc_info,
                       &renderer.main_pass.depth_image.handle,
                       &renderer.main_pass.depth_image.allocation,
                       &renderer.main_pass.depth_image.info) != VK_SUCCESS) {
        LOG_ERROR("Failed to create depth image");
        return false;
    }

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = renderer.main_pass.depth_image.handle;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = renderer.vk.depth_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(renderer.vk.device, &view_info, nullptr,
                          &renderer.main_pass.depth_image.view) != VK_SUCCESS) {
        LOG_ERROR("Failed to create depth image view");
        return false;
    }

    return true;
}

bool core::Engine::init_descriptor_pool() {
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
                               &renderer.vk.descriptor_pool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor pool");
        return false;
    }
    return true;
}

bool core::Engine::init_descriptor_set_layout() {
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
                                    &renderer.vk.descriptor_set_layout) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor set layout");
        return false;
    }
    return true;
}

bool core::Engine::init_command_pool() {
    // Command pool
    VkCommandPoolCreateInfo cmd_pool_info = {};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = renderer.vk.graphics_family_index;
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    if (vkCreateCommandPool(renderer.vk.device.device, &cmd_pool_info, nullptr,
                            &renderer.vk.generic_command_pool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create command pool");
        return false;
    }
    return true;
}

bool core::Engine::init_command_buffers() {
    // Allocate command buffers into per-frame FrameContext
    for (size_t i = 0; i < core::Renderer::MAX_FRAMES_IN_FLIGHT; i++) {
        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = renderer.vk.generic_command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1; // One per frame

        VkResult result =
            vkAllocateCommandBuffers(renderer.vk.device, &alloc_info,
                                     &renderer.frames[i].command_buffer);
        if (result != VK_SUCCESS) {
            LOG_ERROR("Failed to allocate command buffer for frame");
            return false;
        }
    }
    return true;
}

bool core::Engine::init_framebuffers() {
    // We need one framebuffer per swapchain image
    renderer.main_pass.framebuffers.resize(renderer.vk.swapchain.image_count);

    // Transient MSAA color and depth views are created once and shared across
    // all framebuffers
    VkImageView msaa_color_view =
        renderer.main_pass.msaa_color_image.view; // 2D array, 2 layers
    VkImageView depth_view =
        renderer.main_pass.depth_image.view; // 2D array, 2 layers

    for (size_t i = 0; i < renderer.vk.swapchain.image_count; ++i) {
        VkImageView swapchain_view =
            renderer.vk.swapchain.get_image_views().value()[i];

        // The three attachments must match the order defined in your
        // VkRenderPass
        VkImageView attachments[3] = {
            msaa_color_view, // 0: Transient MSAA Color
            swapchain_view,  // 1: Resolved Color (swapchain image)
            depth_view       // 2: Transient Depth
        };

        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = renderer.main_pass.render_pass;
        framebuffer_info.attachmentCount = 3;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = renderer.vk.swap_chain_extent.width;
        framebuffer_info.height = renderer.vk.swap_chain_extent.height;
        framebuffer_info.layers = 1; // Must be 1 with multiview

        if (vkCreateFramebuffer(renderer.vk.device.device, &framebuffer_info,
                                nullptr, &renderer.main_pass.framebuffers[i]) !=
            VK_SUCCESS) {
            LOG_ERROR("Failed to create framebuffer");
            return false;
        }
    }

    return true;
}

bool core::Engine::init_sync_primitives() {
    // Synchronization primitives
    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (auto &frame : renderer.frames) {

        if (vkCreateSemaphore(renderer.vk.device, &semaphore_info, nullptr,
                              &frame.image_available_semaphore) != VK_SUCCESS) {
            LOG_ERROR("Failed to create image available semaphore");
            return false;
        }
        if (vkCreateSemaphore(renderer.vk.device, &semaphore_info, nullptr,
                              &frame.render_finished_semaphore) != VK_SUCCESS) {
            LOG_ERROR("Failed to create render finished semaphore");
            return false;
        }
        if (vkCreateFence(renderer.vk.device, &fence_info, nullptr,
                          &frame.in_flight_fence) != VK_SUCCESS) {
            LOG_ERROR("Failed to create in-flight fence");
            return false;
        }
    }

    return true;
}
