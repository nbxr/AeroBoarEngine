#include "gfx/ShadowMap.h"
#include "gfx/BufferUtils.h"
#include "gfx/Depth.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
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

void image_barrier(VkCommandBuffer cmd, VkImage image, uint32_t layers,
                   VkImageLayout src, VkImageLayout dst, VkAccessFlags src_a,
                   VkAccessFlags dst_a, VkPipelineStageFlags src_st,
                   VkPipelineStageFlags dst_st) {
    VkImageMemoryBarrier bar{};
    bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.srcAccessMask = src_a;
    bar.dstAccessMask = dst_a;
    bar.oldLayout = src;
    bar.newLayout = dst;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = image;
    bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    bar.subresourceRange.levelCount = 1;
    bar.subresourceRange.layerCount = layers;
    vkCmdPipelineBarrier(cmd, src_st, dst_st, 0, 0, nullptr, 0, nullptr, 1, &bar);
}

// Rotation depends only on the light. The eye sits far enough on the light
// side of `origin` that extruded casters stay in front of it. `origin` must
// not follow the camera, or world points slide in light space and a thin
// self-shadow pops.
glm::mat4 stable_light_view(const glm::vec3& to_light, const glm::vec3& origin,
                            float reach) {
    const glm::vec3 up = (std::abs(to_light.y) > 0.99f) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                        : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::lookAt(origin + to_light * reach, origin, up);
}

// Snap the ortho window onto a texel grid whose size only changes in 0.25 mm
// steps. A 1% frustum wobble then does not rescale the map.
void snap_ortho_xy(glm::vec3& mins, glm::vec3& maxs, float resolution) {
    constexpr float kQuantum = 0.00025f;
    const float need = std::max(maxs.x - mins.x, maxs.y - mins.y);
    const float raw = std::max(need / std::max(resolution, 1.0f), kQuantum);
    float texel = std::ceil(raw / kQuantum) * kQuantum;
    const float res = std::max(resolution, 1.0f);
    if (texel * res < need + 2.0f * texel)
        texel += kQuantum;
    const float size = texel * res;
    const float cx = std::floor((0.5f * (mins.x + maxs.x)) / texel) * texel;
    const float cy = std::floor((0.5f * (mins.y + maxs.y)) / texel) * texel;
    mins.x = cx - 0.5f * size;
    maxs.x = cx + 0.5f * size;
    mins.y = cy - 0.5f * size;
    maxs.y = cy + 0.5f * size;
}

// GLM ortho is [0,1] depth and Y-up. Reverse-Z flips the Z row, including its
// translation. Vulkan Y must negate both the scale and the translation;
// negating the scale alone is only right when top == -bottom.
void vulkan_shadow_ortho(glm::mat4& proj) {
    proj[2][2] = -proj[2][2];
    proj[3][2] = 1.0f - proj[3][2];
    proj[1][1] = -proj[1][1];
    proj[3][1] = -proj[3][1];
}

} // namespace

bool ShadowMap::create_image(VkDevice device, VmaAllocator allocator, VkFormat format,
                             uint32_t w, uint32_t h, uint32_t layers, AllocatedImage& out,
                             VkImageViewType view_type, bool sampled) const {
    destroy_image(device, allocator, out);
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {w, h, 1};
    info.mipLevels = 1;
    info.arrayLayers = std::max(1u, layers);
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 (sampled ? (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                          : 0);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    const VkDeviceSize approx_bytes = static_cast<VkDeviceSize>(w) *
                                      static_cast<VkDeviceSize>(h) *
                                      static_cast<VkDeviceSize>(info.arrayLayers) * 4u;
    if (approx_bytes >= (1u << 20))
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
    view.viewType = view_type;
    view.format = format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = info.arrayLayers;
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

    if (!create_image(device, allocator, format_, resolution_, resolution_,
                      kShadowCascades, image_, VK_IMAGE_VIEW_TYPE_2D_ARRAY, true))
        return false;
    if (!create_image(device, allocator, format_, 1, 1, kShadowCascades, dummy_,
                      VK_IMAGE_VIEW_TYPE_2D_ARRAY, true)) {
        destroy(device, allocator);
        return false;
    }
    texel_device_ = device;
    texel_allocator_ = allocator;
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = 4096;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (vmaCreateBuffer(allocator, &bi, &ai, &texel_readback_.buffer,
                            &texel_readback_.allocation,
                            &texel_readback_.info) != VK_SUCCESS) {
            LOG_ERROR("[Shadow] texel readback buffer failed");
            destroy(device, allocator);
            return false;
        }
        texel_readback_.mapped_data = texel_readback_.info.pMappedData;
    }

    for (uint32_t i = 0; i < kShadowCascades; ++i) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image_.handle;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format_;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view.subresourceRange.baseArrayLayer = i;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &view, nullptr, &layer_view_[i]) != VK_SUCCESS) {
            LOG_ERROR("[Shadow] cascade layer view failed");
            destroy(device, allocator);
            return false;
        }
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // Nearest compare: a hardware 2x2 PCF blurs a self-shadow that is only a
    // couple of texels wide down to a gray that flickers as the camera moves.
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    sci.compareEnable = VK_TRUE;
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
    depth_att.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference2 depth_ref{};
    depth_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    depth_ref.attachment = 0;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_ref.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkSubpassDescription2 subpass{};
    subpass.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkRenderPassCreateInfo2 rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &depth_att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    if (vkCreateRenderPass2(device, &rpci, nullptr, &render_pass_) != VK_SUCCESS) {
        LOG_ERROR("[Shadow] render pass create failed");
        destroy(device, allocator);
        return false;
    }

    for (uint32_t i = 0; i < kShadowCascades; ++i) {
        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = render_pass_;
        fbci.attachmentCount = 1;
        fbci.pAttachments = &layer_view_[i];
        fbci.width = resolution_;
        fbci.height = resolution_;
        fbci.layers = 1;
        if (vkCreateFramebuffer(device, &fbci, nullptr, &framebuffer_[i]) !=
            VK_SUCCESS) {
            LOG_ERROR("[Shadow] framebuffer create failed");
            destroy(device, allocator);
            return false;
        }
    }

    if (graphics_queue != VK_NULL_HANDLE && pool != VK_NULL_HANDLE)
        one_shot_clear(device, graphics_queue, pool);

    LOG_VERBOSE("[Shadow] CSM ready " << resolution_ << "x" << resolution_ << " x"
                                   << kShadowCascades);
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
    prepare(cmd, true);
    for (uint32_t i = 0; i < kShadowCascades; ++i) {
        begin(cmd, i);
        end(cmd);
    }
    finish(cmd);
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
    for (uint32_t i = 0; i < kShadowCascades; ++i) {
        if (framebuffer_[i] != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer_[i], nullptr);
            framebuffer_[i] = VK_NULL_HANDLE;
        }
        if (layer_view_[i] != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            vkDestroyImageView(device, layer_view_[i], nullptr);
            layer_view_[i] = VK_NULL_HANDLE;
        }
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
    BufferUtils::destroy_buffer(device, allocator, texel_readback_);
    texel_allocator_ = VK_NULL_HANDLE;
    texel_device_ = VK_NULL_HANDLE;
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
    const glm::vec3 origin = 0.5f * (aabb_min + aabb_max);
    const float reach = glm::length(aabb_max - aabb_min) + 10.0f;
    const glm::mat4 view = stable_light_view(L, origin, reach);

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
    const float res = static_cast<float>(std::max(resolution_, 1u));
    snap_ortho_xy(mins, maxs, res);

    const float z_near = std::max(0.01f, -maxs.z);
    const float z_far = std::max(z_near + 0.05f, -mins.z);
    glm::mat4 proj = glm::ortho(mins.x, maxs.x, mins.y, maxs.y, z_near, z_far);
    vulkan_shadow_ortho(proj);
    return proj * view;
}

glm::mat4 ShadowMap::fit_view_proj_from_corners(const glm::vec3& to_light,
                                                const glm::vec3 corners[8],
                                                const glm::vec3& scene_min,
                                                const glm::vec3& scene_max) const {
    glm::vec3 L = glm::normalize(to_light);
    if (!std::isfinite(L.x) || glm::length(L) < 1e-5f)
        L = glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 sext = glm::max(scene_max - scene_min, glm::vec3(0.05f));
    const float along =
        std::abs(L.x) * sext.x + std::abs(L.y) * sext.y + std::abs(L.z) * sext.z;
    const glm::vec3 origin = 0.5f * (scene_min + scene_max);
    const float reach = glm::length(sext) + along + 10.0f;
    const glm::mat4 view = stable_light_view(L, origin, reach);

    glm::vec3 mins(1.0e9f);
    glm::vec3 maxs(-1.0e9f);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p = corners[i];
        const glm::vec3 toward = p + L * along;
        const glm::vec3 ls0 = glm::vec3(view * glm::vec4(p, 1.0f));
        const glm::vec3 ls1 = glm::vec3(view * glm::vec4(toward, 1.0f));
        mins = glm::min(mins, glm::min(ls0, ls1));
        maxs = glm::max(maxs, glm::max(ls0, ls1));
    }
    if (!std::isfinite(mins.x) || maxs.x - mins.x < 1e-4f || maxs.y - mins.y < 1e-4f)
        return fit_view_proj(to_light, scene_min, scene_max);
    const float res = static_cast<float>(std::max(resolution_, 1u));
    snap_ortho_xy(mins, maxs, res);

    const float z_near = std::max(0.01f, -maxs.z);
    const float z_far = std::max(z_near + 0.05f, -mins.z);
    glm::mat4 proj = glm::ortho(mins.x, maxs.x, mins.y, maxs.y, z_near, z_far);
    vulkan_shadow_ortho(proj);
    return proj * view;
}

bool ShadowMap::frustum_corners(const glm::mat4& view_proj, glm::vec3 out[8]) {
    const glm::mat4 inv = glm::inverse(view_proj);
    int n = 0;
    // Reverse-Z: ndc.z = 1 near, 0 far.
    const float zs[2] = {1.0f, 0.0f};
    for (int z = 0; z < 2; ++z) {
        for (int y = -1; y <= 1; y += 2) {
            for (int x = -1; x <= 1; x += 2) {
                glm::vec4 c = inv * glm::vec4(static_cast<float>(x),
                                              static_cast<float>(y), zs[z], 1.0f);
                if (!std::isfinite(c.x) || !std::isfinite(c.w) || std::abs(c.w) < 1e-8f)
                    return false;
                out[n++] = glm::vec3(c) / c.w;
            }
        }
    }
    return true;
}

void ShadowMap::prepare(VkCommandBuffer cmd, bool from_undefined) {
    if (image_.handle == VK_NULL_HANDLE)
        return;
    const VkImageLayout src = from_undefined
                                  ? VK_IMAGE_LAYOUT_UNDEFINED
                                  : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkAccessFlags src_a = from_undefined ? 0 : VK_ACCESS_SHADER_READ_BIT;
    image_barrier(cmd, image_.handle, kShadowCascades, src,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, src_a,
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
}

void ShadowMap::begin(VkCommandBuffer cmd, uint32_t cascade) {
    cascade = std::min(cascade, kShadowCascades - 1u);
    VkClearValue clear{};
    clear.depthStencil = {kDepthClear, 0};
    VkRenderPassBeginInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = render_pass_;
    info.framebuffer = framebuffer_[cascade];
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

void ShadowMap::finish(VkCommandBuffer cmd) {
    if (image_.handle == VK_NULL_HANDLE)
        return;
    image_barrier(cmd, image_.handle, kShadowCascades,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void ShadowMap::copy_texel(VkCommandBuffer cmd, uint32_t layer, uint32_t x,
                           uint32_t y, uint32_t buffer_offset) {
    if (image_.handle == VK_NULL_HANDLE || texel_readback_.buffer == VK_NULL_HANDLE)
        return;
    layer = std::min(layer, kShadowCascades - 1u);
    x = std::min(x, resolution_ - 1u);
    y = std::min(y, resolution_ - 1u);
    const int32_t ox = static_cast<int32_t>(x > 0 ? x - 1 : 0);
    const int32_t oy = static_cast<int32_t>(y > 0 ? y - 1 : 0);
    const int32_t max_o = static_cast<int32_t>(resolution_ >= 3 ? resolution_ - 3 : 0);
    const int32_t x0 = std::min(ox, max_o);
    const int32_t y0 = std::min(oy, max_o);
    image_barrier(cmd, image_.handle, kShadowCascades,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region{};
    region.bufferOffset = buffer_offset;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    region.imageSubresource.baseArrayLayer = layer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {x0, y0, 0};
    region.imageExtent = {3, 3, 1};
    vkCmdCopyImageToBuffer(cmd, image_.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           texel_readback_.buffer, 1, &region);
    VkBufferMemoryBarrier bb{};
    bb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.buffer = texel_readback_.buffer;
    bb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &bb, 0, nullptr);
    image_barrier(cmd, image_.handle, kShadowCascades,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

float ShadowMap::read_copied_texel() const {
    float t[9];
    read_copied_3x3(t);
    return t[4];
}

void ShadowMap::read_copied_3x3(float out[9], uint32_t float_offset) const {
    for (int i = 0; i < 9; ++i)
        out[i] = -1.0f;
    if (!texel_readback_.mapped_data || !texel_allocator_ ||
        texel_readback_.allocation == VK_NULL_HANDLE)
        return;
    vmaInvalidateAllocation(texel_allocator_, texel_readback_.allocation, 0, 72);
    const auto* p = static_cast<const float*>(texel_readback_.mapped_data);
    for (int i = 0; i < 9; ++i)
        out[i] = p[float_offset + i];
}

void ShadowMap::copy_patch(VkCommandBuffer cmd, uint32_t layer, uint32_t x,
                           uint32_t y, uint32_t n) {
    if (image_.handle == VK_NULL_HANDLE || n == 0 || resolution_ < n)
        return;
    if (!ensure_texel_buffer((VkDeviceSize)n * n * 4u) ||
        texel_readback_.buffer == VK_NULL_HANDLE)
        return;
    layer = std::min(layer, kShadowCascades - 1u);
    x = std::min(x, resolution_ - 1u);
    y = std::min(y, resolution_ - 1u);
    const int32_t half = static_cast<int32_t>(n / 2);
    int32_t x0 = static_cast<int32_t>(x) - half;
    int32_t y0 = static_cast<int32_t>(y) - half;
    const int32_t max_o = static_cast<int32_t>(resolution_ - n);
    x0 = std::max(0, std::min(x0, max_o));
    y0 = std::max(0, std::min(y0, max_o));
    image_barrier(cmd, image_.handle, kShadowCascades,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    region.imageSubresource.baseArrayLayer = layer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {x0, y0, 0};
    region.imageExtent = {n, n, 1};
    vkCmdCopyImageToBuffer(cmd, image_.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           texel_readback_.buffer, 1, &region);
    VkBufferMemoryBarrier bb{};
    bb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.buffer = texel_readback_.buffer;
    bb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &bb, 0, nullptr);
    image_barrier(cmd, image_.handle, kShadowCascades,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

bool ShadowMap::ensure_texel_buffer(VkDeviceSize bytes) {
    if (texel_readback_.buffer != VK_NULL_HANDLE && texel_readback_.info.size >= bytes)
        return true;
    if (!texel_allocator_ || !texel_device_)
        return false;
    BufferUtils::destroy_buffer(texel_device_, texel_allocator_, texel_readback_);
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if (vmaCreateBuffer(texel_allocator_, &bi, &ai, &texel_readback_.buffer,
                        &texel_readback_.allocation, &texel_readback_.info) != VK_SUCCESS)
        return false;
    texel_readback_.mapped_data = texel_readback_.info.pMappedData;
    return true;
}

void ShadowMap::copy_layer(VkCommandBuffer cmd, uint32_t layer) {
    if (image_.handle == VK_NULL_HANDLE || resolution_ == 0)
        return;
    const VkDeviceSize need =
        (VkDeviceSize)resolution_ * (VkDeviceSize)resolution_ * 4u;
    if (!ensure_texel_buffer(need))
        return;
    copy_patch(cmd, layer, resolution_ / 2, resolution_ / 2, resolution_);
}

void ShadowMap::read_patch_minmax(uint32_t n, float& mn, float& mx, uint32_t& n_hi,
                                  float hi_lo) const {
    uint32_t ix = 0, iy = 0;
    read_patch_minmax(n, mn, mx, n_hi, hi_lo, &ix, &iy);
}

void ShadowMap::read_patch_minmax(uint32_t n, float& mn, float& mx, uint32_t& n_hi,
                                  float hi_lo, uint32_t* argmax_x,
                                  uint32_t* argmax_y) const {
    mn = 1.0e9f;
    mx = -1.0e9f;
    n_hi = 0;
    if (argmax_x)
        *argmax_x = 0;
    if (argmax_y)
        *argmax_y = 0;
    if (!texel_readback_.mapped_data || !texel_allocator_ ||
        texel_readback_.allocation == VK_NULL_HANDLE || n == 0)
        return;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(n) * n * 4u;
    vmaInvalidateAllocation(texel_allocator_, texel_readback_.allocation, 0, bytes);
    const auto* p = static_cast<const float*>(texel_readback_.mapped_data);
    const uint32_t count = n * n;
    uint32_t imax = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const float v = p[i];
        mn = std::min(mn, v);
        if (v > mx) {
            mx = v;
            imax = i;
        }
        if (v >= hi_lo)
            ++n_hi;
    }
    if (argmax_x)
        *argmax_x = imax % n;
    if (argmax_y)
        *argmax_y = imax / n;
}

} // namespace gfx
