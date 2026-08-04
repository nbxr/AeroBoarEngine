#include "gfx/Engine.h"
#include "gfx/AllocatedBuffer.h"
#include "gfx/AllocatedImage.h"
#include "gfx/Renderer.h"
#include "core/Log.h"

bool gfx::Engine::init_render_pass() {
    // Attachments (MSAA path — default on desktop):
    // 0 MSAA color (transient) | 1 swapchain resolve | 2 MSAA depth (transient)
    // 3 resolved single-sample depth (STORE → Hi-Z source)
    const bool msaa = renderer.vk.msaa_color != VK_SAMPLE_COUNT_1_BIT;
    renderer.main_pass.uses_depth_resolve = msaa;

    VkAttachmentDescription2 color_attachment{};
    color_attachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    color_attachment.format = renderer.vk.swap_chain_image_format;
    color_attachment.samples = renderer.vk.msaa_color;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription2 swapchain_attachment{};
    swapchain_attachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    swapchain_attachment.format = renderer.vk.swap_chain_image_format;
    swapchain_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    swapchain_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    swapchain_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    swapchain_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    swapchain_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    swapchain_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapchain_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription2 depth_attachment{};
    depth_attachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    depth_attachment.format = renderer.vk.depth_format;
    depth_attachment.samples = renderer.vk.msaa_depth;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp =
        msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout =
        msaa ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
             : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription2 depth_resolve_attachment{};
    depth_resolve_attachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    depth_resolve_attachment.format = renderer.vk.depth_format;
    depth_resolve_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_resolve_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_resolve_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_resolve_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_resolve_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_resolve_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_resolve_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference2 color_ref{};
    color_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_ref.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkAttachmentReference2 resolve_ref{};
    resolve_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    resolve_ref.attachment = 1;
    resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    resolve_ref.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkAttachmentReference2 depth_ref{};
    depth_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    depth_ref.attachment = 2;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_ref.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkAttachmentReference2 depth_resolve_ref{};
    depth_resolve_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    depth_resolve_ref.attachment = 3;
    depth_resolve_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_resolve_ref.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    // Prefer MIN depth resolve for Hi-Z (closest surface, 0=near). Fall back to
    // SAMPLE_ZERO if the device does not advertise MIN.
    VkResolveModeFlagBits depth_resolve_mode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    {
        VkPhysicalDeviceDepthStencilResolveProperties ds_props{};
        ds_props.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &ds_props;
        vkGetPhysicalDeviceProperties2(renderer.vk.device.physical_device, &props2);
        if (ds_props.supportedDepthResolveModes & VK_RESOLVE_MODE_MIN_BIT) {
            depth_resolve_mode = VK_RESOLVE_MODE_MIN_BIT;
            LOG_INFO("[HiZ] Depth MSAA resolve mode: MIN (best for occlusion)");
        } else {
            LOG_INFO("[HiZ] Depth MSAA resolve mode: SAMPLE_ZERO (MIN unsupported)");
        }
    }

    VkSubpassDescriptionDepthStencilResolve depth_resolve{};
    depth_resolve.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE;
    depth_resolve.depthResolveMode = depth_resolve_mode;
    depth_resolve.stencilResolveMode = VK_RESOLVE_MODE_NONE;
    depth_resolve.pDepthStencilResolveAttachment = &depth_resolve_ref;

    VkSubpassDescription2 subpass{};
    subpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;
    if (msaa) {
        subpass.pResolveAttachments = &resolve_ref;
        subpass.pNext = &depth_resolve;
    }

    // Non-MSAA: swapchain color + depth only (depth is HZB source).
    VkAttachmentDescription2 color_1x = swapchain_attachment;
    color_1x.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_1x.samples = VK_SAMPLE_COUNT_1_BIT;
    VkAttachmentReference2 color_1x_ref = color_ref;
    color_1x_ref.attachment = 0;
    VkAttachmentReference2 depth_1x_ref = depth_ref;
    depth_1x_ref.attachment = 1;
    depth_attachment.samples = renderer.vk.msaa_depth; // 1 when !msaa
    VkAttachmentDescription2 attachments_ss[2] = {color_1x, depth_attachment};
    VkSubpassDescription2 subpass_ss{};
    subpass_ss.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    subpass_ss.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_ss.colorAttachmentCount = 1;
    subpass_ss.pColorAttachments = &color_1x_ref;
    subpass_ss.pDepthStencilAttachment = &depth_1x_ref;

    VkSubpassDependency2 dep_in{};
    dep_in.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dep_in.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep_in.dstSubpass = 0;
    dep_in.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dep_in.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_SHADER_READ_BIT;
    dep_in.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep_in.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency2 dep_out{};
    dep_out.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dep_out.srcSubpass = 0;
    dep_out.dstSubpass = VK_SUBPASS_EXTERNAL;
    dep_out.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep_out.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep_out.dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dep_out.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkSubpassDependency2 deps[] = {dep_in, dep_out};

    VkAttachmentDescription2 attachments_msaa[4] = {
        color_attachment, swapchain_attachment, depth_attachment,
        depth_resolve_attachment};

    VkRenderPassCreateInfo2 rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    if (msaa) {
        rpci.attachmentCount = 4;
        rpci.pAttachments = attachments_msaa;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass;
    } else {
        rpci.attachmentCount = 2;
        rpci.pAttachments = attachments_ss;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass_ss;
    }

    if (vkCreateRenderPass2(renderer.vk.device, &rpci, nullptr,
                            &renderer.main_pass.render_pass) != VK_SUCCESS) {
        LOG_ERROR("Failed to create render pass (vkCreateRenderPass2)");
        return false;
    }
    return true;
}

bool gfx::Engine::create_msaa_color_image(VkExtent2D extent, AllocatedImage& out_image) {
    // Create transient MSAA color image (on-chip only, no DRAM writes)
    VkImageCreateInfo color_image_info = {};
    color_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    color_image_info.imageType = VK_IMAGE_TYPE_2D;
    color_image_info.format = renderer.vk.swap_chain_image_format;
    color_image_info.extent = {extent.width, extent.height, 1};
    color_image_info.mipLevels = 1;
    color_image_info.arrayLayers = 1;
    color_image_info.samples = renderer.vk.msaa_color;
    color_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    color_image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    color_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    color_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(renderer.allocator, &color_image_info, &alloc_info,
                       &out_image.handle,
                       &out_image.allocation,
                       &out_image.info) != VK_SUCCESS) {
        LOG_ERROR("Failed to create MSAA color image");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out_image.handle;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = renderer.vk.swap_chain_image_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(renderer.vk.device, &view_info, nullptr, &out_image.view) != VK_SUCCESS) {
        LOG_ERROR("Failed to create MSAA color image view");
        vmaDestroyImage(renderer.allocator, out_image.handle, out_image.allocation);
        out_image = {};
        return false;
    }

    return true;
}

bool gfx::Engine::init_msaa_color_image() {
    const uint32_t n = static_cast<uint32_t>(renderer.vk.swap_chain_image_views.size());
    renderer.main_pass.msaa_color_images.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (!create_msaa_color_image(renderer.vk.swap_chain_extent, renderer.main_pass.msaa_color_images[i])) {
            return false;
        }
    }
    return true;
}

bool gfx::Engine::create_depth_image(VkExtent2D extent, AllocatedImage& out_image) {
    // MSAA: transient. Single-sample: storeable + sampled (Hi-Z source).
    const bool msaa = renderer.vk.msaa_depth != VK_SAMPLE_COUNT_1_BIT;
    VkImageCreateInfo depth_image_info = {};
    depth_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depth_image_info.imageType = VK_IMAGE_TYPE_2D;
    depth_image_info.format = renderer.vk.depth_format;
    depth_image_info.extent = {extent.width, extent.height, 1};
    depth_image_info.mipLevels = 1;
    depth_image_info.arrayLayers = 1;
    depth_image_info.samples = renderer.vk.msaa_depth;
    depth_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    depth_image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (msaa)
        depth_image_info.usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    else
        depth_image_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    depth_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depth_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(renderer.allocator, &depth_image_info, &alloc_info,
                       &out_image.handle,
                       &out_image.allocation,
                       &out_image.info) != VK_SUCCESS) {
        LOG_ERROR("Failed to create depth image");
        return false;
    }

    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out_image.handle;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = renderer.vk.depth_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(renderer.vk.device, &view_info, nullptr, &out_image.view) != VK_SUCCESS) {
        LOG_ERROR("Failed to create depth image view");
        vmaDestroyImage(renderer.allocator, out_image.handle, out_image.allocation);
        out_image = {};
        return false;
    }

    return true;
}

bool gfx::Engine::create_resolved_depth_image(VkExtent2D extent,
                                              AllocatedImage& out_image) {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = renderer.vk.depth_format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(renderer.allocator, &info, &alloc_info, &out_image.handle,
                       &out_image.allocation, &out_image.info) != VK_SUCCESS) {
        LOG_ERROR("Failed to create resolved depth image");
        return false;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out_image.handle;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = renderer.vk.depth_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(renderer.vk.device, &view_info, nullptr, &out_image.view) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create resolved depth image view");
        vmaDestroyImage(renderer.allocator, out_image.handle, out_image.allocation);
        out_image = {};
        return false;
    }
    return true;
}

bool gfx::Engine::init_depth_image() {
    const uint32_t n = static_cast<uint32_t>(renderer.vk.swap_chain_image_views.size());
    renderer.main_pass.depth_images.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (!create_depth_image(renderer.vk.swap_chain_extent, renderer.main_pass.depth_images[i])) {
            return false;
        }
    }

    if (renderer.main_pass.uses_depth_resolve) {
        renderer.main_pass.resolved_depth_images.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            if (!create_resolved_depth_image(
                    renderer.vk.swap_chain_extent,
                    renderer.main_pass.resolved_depth_images[i])) {
                return false;
            }
        }
    } else {
        renderer.main_pass.resolved_depth_images.clear();
    }
    return true;
}

bool gfx::Engine::create_prepass_depth_image(VkExtent2D extent, AllocatedImage& out_image) {
    // Single-sample depth for same-frame Hi-Z (attachment + sampled by hzb_copy).
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = renderer.vk.depth_format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(renderer.allocator, &info, &alloc_info, &out_image.handle,
                       &out_image.allocation, &out_image.info) != VK_SUCCESS) {
        LOG_ERROR("Failed to create depth-prepass image");
        return false;
    }

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out_image.handle;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = renderer.vk.depth_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(renderer.vk.device, &view_info, nullptr, &out_image.view) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create depth-prepass image view");
        vmaDestroyImage(renderer.allocator, out_image.handle, out_image.allocation);
        out_image = {};
        return false;
    }
    return true;
}

bool gfx::Engine::init_depth_prepass() {
    // Depth-only render pass → SHADER_READ_ONLY for Hi-Z copy (same frame).
    VkAttachmentDescription2 depth_att{};
    depth_att.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    depth_att.format = renderer.vk.depth_format;
    depth_att.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference2 depth_ref{};
    depth_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    depth_ref.attachment = 0;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_ref.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkSubpassDescription2 subpass{};
    subpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency2 dep_in{};
    dep_in.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dep_in.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep_in.dstSubpass = 0;
    dep_in.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep_in.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep_in.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep_in.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkSubpassDependency2 dep_out{};
    dep_out.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dep_out.srcSubpass = 0;
    dep_out.dstSubpass = VK_SUBPASS_EXTERNAL;
    dep_out.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep_out.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep_out.dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dep_out.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkSubpassDependency2 deps[] = {dep_in, dep_out};

    VkRenderPassCreateInfo2 rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &depth_att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;

    if (vkCreateRenderPass2(renderer.vk.device, &rpci, nullptr,
                            &renderer.depth_prepass.render_pass) != VK_SUCCESS) {
        LOG_ERROR("Failed to create depth-prepass render pass");
        return false;
    }

    // One depth target + framebuffer per frame-in-flight (concurrent frames).
    const uint32_t n = Renderer::MAX_FRAMES_IN_FLIGHT;
    renderer.depth_prepass.depth_images.resize(n);
    renderer.depth_prepass.framebuffers.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (!create_prepass_depth_image(renderer.vk.swap_chain_extent,
                                        renderer.depth_prepass.depth_images[i])) {
            return false;
        }

        VkImageView att = renderer.depth_prepass.depth_images[i].view;
        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = renderer.depth_prepass.render_pass;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &att;
        fbci.width = renderer.vk.swap_chain_extent.width;
        fbci.height = renderer.vk.swap_chain_extent.height;
        fbci.layers = 1;
        if (vkCreateFramebuffer(renderer.vk.device, &fbci, nullptr,
                                &renderer.depth_prepass.framebuffers[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create depth-prepass framebuffer");
            return false;
        }
    }

    LOG_INFO("[HiZ] Depth prepass ready (" << renderer.vk.swap_chain_extent.width
             << "x" << renderer.vk.swap_chain_extent.height << ", 1x samples)");
    return true;
}

bool gfx::Engine::init_descriptor_pool() {
    // Per-frame bindless sets. Each set needs:
    //   - kMaxBindlessTextures (variable array on binding 9)
    //   - 2 fixed IBL samplers (env cube + BRDF LUT on bindings 7–8)
    // Previously the pool was sized only for the texture array, so allocating
    // the second set failed BestPractices-EmptyDescriptorPoolType (10002 needed,
    // ~9998 remaining after the first set).
    const uint32_t frames = Renderer::MAX_FRAMES_IN_FLIGHT;
    constexpr uint32_t kMaxBindlessTextures = 10000;
    constexpr uint32_t kFixedImageSamplersPerSet = 2; // IBL cube + BRDF LUT
    const uint32_t image_samplers_per_set =
        kMaxBindlessTextures + kFixedImageSamplersPerSet;
    // Storage: FrameConstants not counted here; lights + scene tables + extras
    const uint32_t storage_per_set = 8;

    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 * frames},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         image_samplers_per_set * frames + 16},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 * frames},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storage_per_set * frames + 16}};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 4;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = frames + 4;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

    if (vkCreateDescriptorPool(renderer.vk.device, &pool_info, nullptr,
                               &renderer.vk.descriptor_pool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor pool");
        return false;
    }
    return true;
}

bool gfx::Engine::init_descriptor_set_layout() {
    // Global bindless descriptor layout.
    // VARIABLE_DESCRIPTOR_COUNT (textures) MUST be the highest binding number.
    // 0: FrameConstants | 1-5: scene | 6: lights | 7: env cube | 8: BRDF LUT
    // 9: joint matrices | 10: textures
    VkDescriptorSetLayoutBinding bindings[11] = {};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    // 6: Lights SSBO
    bindings[6].binding = Renderer::BINDING_LIGHTS;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    // 7: Prefiltered specular environment cubemap
    bindings[7].binding = Renderer::BINDING_IBL_SPECULAR;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // 8: BRDF integration LUT (2D)
    bindings[8].binding = Renderer::BINDING_IBL_BRDF_LUT;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // 9: Joint matrices (skinning palette)
    bindings[9].binding = Renderer::BINDING_JOINT_MATRICES;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // 10: Textures — MUST be highest binding
    bindings[10].binding = Renderer::BINDING_TEXTURES;
    bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[10].descriptorCount = 10000;
    bindings[10].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{};
    binding_flags_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;

    VkDescriptorBindingFlags binding_flags[11] = {};
    binding_flags[0] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[1] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[2] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[3] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[4] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[5] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[6] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[7] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[8] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[9] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[10] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

    binding_flags_info.bindingCount = 11;
    binding_flags_info.pBindingFlags = binding_flags;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 11;
    layout_info.pBindings = bindings;
    layout_info.flags =
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layout_info.pNext = &binding_flags_info;

    if (vkCreateDescriptorSetLayout(renderer.vk.device, &layout_info, nullptr,
                                    &renderer.vk.descriptor_set_layout) !=
        VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor set layout");
        return false;
    }
    return true;
}

bool gfx::Engine::init_bindless_descriptor_set() {
    if (renderer.vk.descriptor_pool == VK_NULL_HANDLE ||
        renderer.vk.descriptor_set_layout == VK_NULL_HANDLE) {
        LOG_ERROR("Descriptor pool or layout not ready for bindless set");
        return false;
    }

    const uint32_t num_sets = Renderer::MAX_FRAMES_IN_FLIGHT;
    renderer.vk.bindless_descriptor_sets.resize(num_sets);

    for (uint32_t i = 0; i < num_sets; ++i) {
        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = renderer.vk.descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &renderer.vk.descriptor_set_layout;

        // Variable count applies to the highest binding (textures = 7)
        VkDescriptorSetVariableDescriptorCountAllocateInfo var_info{};
        var_info.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        var_info.descriptorSetCount = 1;
        uint32_t counts[] = {10000};
        var_info.pDescriptorCounts = counts;
        alloc_info.pNext = &var_info;

        if (vkAllocateDescriptorSets(renderer.vk.device, &alloc_info,
                                     &renderer.vk.bindless_descriptor_sets[i]) !=
            VK_SUCCESS) {
            LOG_ERROR("Failed to allocate bindless descriptor set " << i);
            return false;
        }
    }

    return true;
}

bool gfx::Engine::init_command_pool() {
    // Command pool
    VkCommandPoolCreateInfo cmd_pool_info = {};
    cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmd_pool_info.queueFamilyIndex = renderer.vk.graphics_family_index;
    // We reset command buffers every frame, so we need RESET_COMMAND_BUFFER_BIT.
    // TRANSIENT_BIT is still useful for short-lived buffers.
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                          VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(renderer.vk.device.device, &cmd_pool_info, nullptr,
                            &renderer.vk.generic_command_pool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create command pool");
        return false;
    }
    return true;
}

bool gfx::Engine::init_command_buffers() {
    // Allocate command buffers into per-frame FrameContext
    for (size_t i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; i++) {
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

bool gfx::Engine::init_framebuffers() {
    renderer.main_pass.framebuffers.resize(renderer.vk.swap_chain_image_views.size());

    for (size_t i = 0; i < renderer.vk.swap_chain_image_views.size(); ++i) {
        VkImageView swapchain_view = renderer.vk.swap_chain_image_views[i];
        VkImageView depth_view = renderer.main_pass.depth_images[i].view;

        VkImageView attachments[4]{};
        uint32_t attachment_count = 0;

        if (renderer.main_pass.uses_depth_resolve) {
            attachments[0] = renderer.main_pass.msaa_color_images[i].view;
            attachments[1] = swapchain_view;
            attachments[2] = depth_view;
            attachments[3] = renderer.main_pass.resolved_depth_images[i].view;
            attachment_count = 4;
        } else {
            attachments[0] = swapchain_view;
            attachments[1] = depth_view;
            attachment_count = 2;
        }

        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = renderer.main_pass.render_pass;
        framebuffer_info.attachmentCount = attachment_count;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = renderer.vk.swap_chain_extent.width;
        framebuffer_info.height = renderer.vk.swap_chain_extent.height;
        framebuffer_info.layers = 1;

        if (vkCreateFramebuffer(renderer.vk.device.device, &framebuffer_info,
                                nullptr, &renderer.main_pass.framebuffers[i]) !=
            VK_SUCCESS) {
            LOG_ERROR("Failed to create framebuffer");
            return false;
        }
    }

    return true;
}

bool gfx::Engine::init_sync_primitives() {
    // Fences are per frame-in-flight
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (auto &frame : renderer.frames) {
        if (vkCreateFence(renderer.vk.device, &fence_info, nullptr,
                          &frame.in_flight_fence) != VK_SUCCESS) {
            LOG_ERROR("Failed to create in-flight fence");
            return false;
        }
    }

    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // image_available_semaphores: one per frame in flight (protected by fences)
    const uint32_t frame_count = Renderer::MAX_FRAMES_IN_FLIGHT;
    renderer.vk.image_available_semaphores.resize(frame_count);
    for (uint32_t i = 0; i < frame_count; ++i) {
        if (vkCreateSemaphore(renderer.vk.device, &semaphore_info, nullptr,
                              &renderer.vk.image_available_semaphores[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create image available semaphore");
            return false;
        }
    }

    // render_finished_semaphores: one per actual swapchain image
    // This solves the "semaphore still in use by present" problem when drivers return > MAX_FRAMES images
    const uint32_t image_count = static_cast<uint32_t>(renderer.vk.swap_chain_image_views.size());
    if (image_count == 0) {
        LOG_ERROR("No swapchain images available when creating semaphores");
        return false;
    }

    renderer.vk.render_finished_semaphores.resize(image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        if (vkCreateSemaphore(renderer.vk.device, &semaphore_info, nullptr,
                              &renderer.vk.render_finished_semaphores[i]) != VK_SUCCESS) {
            LOG_ERROR("Failed to create render finished semaphore");
            return false;
        }
    }

    return true;
}
