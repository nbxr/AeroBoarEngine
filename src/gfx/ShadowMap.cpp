#include "gfx/ShadowMap.h"
#include "gfx/Depth.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace gfx {
namespace {

void destroy_image(VkDevice device, VmaAllocator allocator, AllocatedImage& img) {
    if (img.view != VK_NULL_HANDLE && device != VK_NULL_HANDLE)
        vkDestroyImageView(device, img.view, nullptr);
    if (img.handle != VK_NULL_HANDLE && allocator)
        vmaDestroyImage(allocator, img.handle, img.allocation);
    img = {};
}

} // namespace

bool ShadowMap::create_image(VkDevice device, VmaAllocator allocator, VkFormat format,
                             uint32_t w, uint32_t h, AllocatedImage& out,
                             bool sampled) const {
    destroy_image(device, allocator, out);
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {w, h, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 (sampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    alloc.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    if (vmaCreateImage(allocator, &info, &alloc, &out.handle, &out.allocation,
                       &out.info) != VK_SUCCESS) {
        LOG_ERROR("[Shadow] image create failed");
        out = {};
        return false;
    }

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = out.handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &view, nullptr, &out.view) != VK_SUCCESS) {
        LOG_ERROR("[Shadow] image view failed");
        vmaDestroyImage(allocator, out.handle, out.allocation);
        out = {};
        return false;
    }
    return true;
}

bool ShadowMap::create(VkDevice device, VmaAllocator allocator, VkFormat depth_format,
                       uint32_t resolution, VkQueue graphics_queue, VkCommandPool pool) {
    destroy(device, allocator);
    format_ = depth_format;
    resolution_ = std::max(64u, resolution);

    if (!create_image(device, allocator, format_, resolution_, resolution_, image_,
                      true))
        return false;
    if (!create_image(device, allocator, format_, 1, 1, dummy_, true)) {
        destroy(device, allocator);
        return false;
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK; // reverse-Z far = 0
    sci.compareEnable = VK_TRUE;
    // Reverse-Z: 1 = near. Lit when stored (closest caster) <= receiver, i.e.
    // Dref >= Dtexel. LESS_OR_EQUAL inverts the map (lit areas go dark).
    sci.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    sci.maxLod = 0.0f;
    if (vkCreateSampler(device, &sci, nullptr, &sampler_) != VK_SUCCESS) {
        LOG_ERROR("[Shadow] sampler create failed");
        destroy(device, allocator);
        return false;
    }

    VkAttachmentDescription2 depth_att{};
    depth_att.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    depth_att.format = format_;
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
    subpass.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency2 dep_in{};
    dep_in.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    dep_in.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep_in.dstSubpass = 0;
    dep_in.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dep_in.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
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
    dep_out.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
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
    if (vkCreateRenderPass2(device, &rpci, nullptr, &render_pass_) != VK_SUCCESS) {
        LOG_ERROR("[Shadow] render pass create failed");
        destroy(device, allocator);
        return false;
    }

    VkFramebufferCreateInfo fbci{};
    fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbci.renderPass = render_pass_;
    fbci.attachmentCount = 1;
    fbci.pAttachments = &image_.view;
    fbci.width = resolution_;
    fbci.height = resolution_;
    fbci.layers = 1;
    if (vkCreateFramebuffer(device, &fbci, nullptr, &framebuffer_) != VK_SUCCESS) {
        LOG_ERROR("[Shadow] framebuffer create failed");
        destroy(device, allocator);
        return false;
    }

    if (graphics_queue != VK_NULL_HANDLE && pool != VK_NULL_HANDLE)
        one_shot_clear(device, graphics_queue, pool);

    LOG_INFO("[Shadow] map ready " << resolution_ << "x" << resolution_);
    return true;
}

bool ShadowMap::one_shot_clear(VkDevice device, VkQueue queue, VkCommandPool pool) {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &alloc, &cmd) != VK_SUCCESS)
        return false;
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    this->begin(cmd);
    this->end(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    return true;
}

void ShadowMap::destroy(VkDevice device, VmaAllocator allocator) {
    if (pipeline_ != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (framebuffer_ != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
    if (render_pass_ != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    destroy_image(device, allocator, image_);
    destroy_image(device, allocator, dummy_);
    resolution_ = 0;
}

void ShadowMap::bind_descriptor(VkDevice device, VkDescriptorSet set,
                                uint32_t binding) const {
    if (!sampler_ || set == VK_NULL_HANDLE)
        return;
    const VkImageView view = image_.view != VK_NULL_HANDLE ? image_.view : dummy_.view;
    if (view == VK_NULL_HANDLE)
        return;
    VkDescriptorImageInfo img{};
    img.sampler = sampler_;
    img.imageView = view;
    img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set;
    w.dstBinding = binding;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &img;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

glm::mat4 ShadowMap::fit_view_proj(const glm::vec3& to_light, const glm::vec3& aabb_min,
                                   const glm::vec3& aabb_max) const {
    glm::vec3 L = glm::normalize(to_light);
    if (!std::isfinite(L.x) || glm::length(L) < 1e-5f)
        L = glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 up =
        (std::abs(L.y) > 0.99f) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 center = 0.5f * (aabb_min + aabb_max);
    const glm::vec3 ext = glm::max(aabb_max - aabb_min, glm::vec3(0.05f));
    const float r = 0.5f * glm::length(ext);
    const glm::vec3 eye = center + L * (r * 2.0f + 1.0f);
    const glm::mat4 view = glm::lookAt(eye, center, up);

    glm::vec3 mins(1.0e9f);
    glm::vec3 maxs(-1.0e9f);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p = glm::vec3((i & 1) ? aabb_max.x : aabb_min.x,
                                      (i & 2) ? aabb_max.y : aabb_min.y,
                                      (i & 4) ? aabb_max.z : aabb_min.z);
        const glm::vec3 ls = glm::vec3(view * glm::vec4(p, 1.0f));
        mins = glm::min(mins, ls);
        maxs = glm::max(maxs, ls);
    }
    const float margin = r * 0.02f + 0.02f;
    mins -= glm::vec3(margin);
    maxs += glm::vec3(margin);

    const float res = static_cast<float>(std::max(resolution_, 1u));
    const float texel_x = std::max(maxs.x - mins.x, 1e-3f) / res;
    const float texel_y = std::max(maxs.y - mins.y, 1e-3f) / res;
    mins.x = std::floor(mins.x / texel_x) * texel_x;
    mins.y = std::floor(mins.y / texel_y) * texel_y;
    maxs.x = mins.x + std::ceil(std::max(maxs.x - mins.x, texel_x) / texel_x) * texel_x;
    maxs.y = mins.y + std::ceil(std::max(maxs.y - mins.y, texel_y) / texel_y) * texel_y;

    // lookAt: scene is in -Z. Positive ortho near/far distances:
    const float z_near = std::max(0.01f, -maxs.z);
    const float z_far = std::max(z_near + 0.05f, -mins.z);
    glm::mat4 proj = glm::ortho(mins.x, maxs.x, mins.y, maxs.y, z_near, z_far);
    proj[2][2] = -proj[2][2];
    proj[3][2] = 1.0f - proj[3][2];
    proj[1][1] *= -1.0f;
    return proj * view;
}

void ShadowMap::begin(VkCommandBuffer cmd) {
    VkClearValue clear{};
    clear.depthStencil = {kDepthClear, 0};
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = render_pass_;
    info.framebuffer = framebuffer_;
    info.renderArea.extent = {resolution_, resolution_};
    info.clearValueCount = 1;
    info.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.width = static_cast<float>(resolution_);
    vp.height = static_cast<float>(resolution_);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{};
    scissor.extent = {resolution_, resolution_};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void ShadowMap::end(VkCommandBuffer cmd) { vkCmdEndRenderPass(cmd); }

} // namespace gfx
