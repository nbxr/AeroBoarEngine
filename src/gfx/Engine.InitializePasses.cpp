#include "gfx/Engine.h"
#include "gfx/AllocatedBuffer.h"
#include "gfx/AllocatedImage.h"
#include "gfx/Renderer.h"

bool gfx::Engine::init_render_pass() {
    // Multiview render pass for Quest 3
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = renderer.vk.swap_chain_image_format;
    color_attachment.samples = renderer.vk.msaa_color;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // Transient MSAA
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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

    // Resolve target (swapchain)
    VkAttachmentReference resolve_attachment_ref = {};
    resolve_attachment_ref.attachment = 1;
    resolve_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref = {};
    depth_attachment_ref.attachment = 2;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
    subpass.pResolveAttachments = &resolve_attachment_ref;  // MSAA resolve to swapchain
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    // External subpass dependency.
    // This tells Vulkan (and sync validation) what previous work must complete
    // before this render pass can begin its implicit layout transitions and
    // loadOp clears (including the depth clear on attachment 2).
    //
    // The previous version only covered color. That was the source of the
    // SYNC-HAZARD-WRITE-AFTER-WRITE errors on the depth attachment and the
    // cross-frame hazard between EndRenderPass and the next BeginRenderPass.
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

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
    // Create transient depth image (no DRAM writes, DONT_CARE store)
    VkImageCreateInfo depth_image_info = {};
    depth_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depth_image_info.imageType = VK_IMAGE_TYPE_2D;
    depth_image_info.format = renderer.vk.depth_format;
    depth_image_info.extent = {extent.width, extent.height, 1};
    depth_image_info.mipLevels = 1;
    depth_image_info.arrayLayers = 1;
    depth_image_info.samples = renderer.vk.msaa_depth;
    depth_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    depth_image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
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

bool gfx::Engine::init_depth_image() {
    const uint32_t n = static_cast<uint32_t>(renderer.vk.swap_chain_image_views.size());
    renderer.main_pass.depth_images.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (!create_depth_image(renderer.vk.swap_chain_extent, renderer.main_pass.depth_images[i])) {
            return false;
        }
    }
    return true;
}

bool gfx::Engine::init_descriptor_pool() {
    // Descriptor pool sized for multiple per-frame bindless sets (one per MAX_FRAMES_IN_FLIGHT).
    // Each set has its own copy of the variable-count texture array (binding 6, 10000 entries)
    // plus the other static bindings. The large sampler pool size must account for all sets.
    const uint32_t frames = Renderer::MAX_FRAMES_IN_FLIGHT;
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10000 * frames},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000}};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 4;
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = 1000 * frames;  // generous
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
    // The binding with VARIABLE_DESCRIPTOR_COUNT (the large texture array) **must**
    // be the highest binding number per Vulkan spec.
    // Binding 0: per-frame UBO (future)
    // Bindings 1-5: global scene data tables as storage buffers (for GPU-driven
    //               culling + vertex pulling). Updated at load time (and later
    //               per-frame for dynamic objects) via UPDATE_AFTER_BIND.
    // Binding 6: bindless texture array (combined image+sampler) - variable count, last
    VkDescriptorSetLayoutBinding bindings[7] = {};

    // 0: UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // 1: Scene instances (transforms + material/mesh indices) - SSBO
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    // 2: Materials (PBR params + texture indices) - SSBO
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    // 3: Mesh primitive metadata (offsets into vertex/index buffers) - SSBO
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    // 4: Vertex buffer (SSBO view; primary path now uses proper vertex attributes)
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    // 5: Index buffer (storage view for indexed draws / pulling)
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT |
                             VK_SHADER_STAGE_COMPUTE_BIT;

    // 6: Textures (large variable array) - MUST be highest binding number
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[6].descriptorCount = 10000;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Per-binding flags for update-after-bind + variable count on textures
    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_info{};
    binding_flags_info.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;

    VkDescriptorBindingFlags binding_flags[7] = {};
    binding_flags[0] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    binding_flags[1] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;  // instances
    binding_flags[2] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;  // materials
    binding_flags[3] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;  // mesh meta
    binding_flags[4] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;  // vertices
    binding_flags[5] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;  // indices
    binding_flags[6] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                       VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                       VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;  // textures (last)

    binding_flags_info.bindingCount = 7;
    binding_flags_info.pBindingFlags = binding_flags;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 7;
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

    // Prepare one alloc info per set (or allocate in batch).
    // For simplicity and to keep the variable count info per-set, we allocate one by one.
    for (uint32_t i = 0; i < num_sets; ++i) {
        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = renderer.vk.descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &renderer.vk.descriptor_set_layout;

        // Chain variable descriptor count for the large bindless texture array (binding 6)
        VkDescriptorSetVariableDescriptorCountAllocateInfo var_info{};
        var_info.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
        var_info.descriptorSetCount = 1;
        uint32_t counts[] = {10000}; // matches layout binding 6 descriptorCount
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
    // We need one framebuffer per swapchain image
    renderer.main_pass.framebuffers.resize(renderer.vk.swap_chain_image_views.size());

    // One set of transient MSAA + depth per swapchain image so concurrent
    // in-flight frames (different acquired images) don't overlap on the same
    // transient attachments.
    for (size_t i = 0; i < renderer.vk.swap_chain_image_views.size(); ++i) {
        VkImageView swapchain_view =
            renderer.vk.swap_chain_image_views[i];

        VkImageView msaa_color_view = renderer.main_pass.msaa_color_images[i].view;
        VkImageView depth_view      = renderer.main_pass.depth_images[i].view;

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
