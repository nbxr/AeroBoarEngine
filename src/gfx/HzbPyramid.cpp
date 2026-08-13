#include "gfx/HzbPyramid.h"
#include "gfx/ShaderLoader.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace gfx {
namespace {

VkShaderModule load_module(VkDevice device, const char* path) {
    std::vector<unsigned int> code;
    if (!load_shader_source(path, code))
        return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size() * sizeof(unsigned int);
    ci.pCode = code.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return mod;
}

uint32_t calc_mip_count(uint32_t w, uint32_t h) {
    uint32_t m = 1;
    while (w > 1 || h > 1) {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        ++m;
        if (m >= HzbPyramid::kMaxMips)
            break;
    }
    return m;
}

} // namespace

bool HzbPyramid::initialize(VkDevice device, VmaAllocator /*allocator*/) {
    device_ = device;

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.minLod = 0.0f;
    sci.maxLod = float(kMaxMips);
    if (vkCreateSampler(device, &sci, nullptr, &sampler_) != VK_SUCCESS)
        return false;
    if (vkCreateSampler(device, &sci, nullptr, &depth_sampler_) != VK_SUCCESS)
        return false;

    // One layout for copy + reduce: binding0 combined, binding1 storage write.
    VkDescriptorSetLayoutBinding copy_b[2]{};
    copy_b[0].binding = 0;
    copy_b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    copy_b[0].descriptorCount = 1;
    copy_b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    copy_b[1].binding = 1;
    copy_b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    copy_b[1].descriptorCount = 1;
    copy_b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 2;
    lci.pBindings = copy_b;
    if (vkCreateDescriptorSetLayout(device, &lci, nullptr, &set_layout_) != VK_SUCCESS)
        return false;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(uint32_t) * 4; // uvec2 + uvec2

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &set_layout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &pipeline_layout_) != VK_SUCCESS)
        return false;

    if (!create_pipelines(device))
        return false;

    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFrames * kMaxMips},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxFrames * kMaxMips},
    };
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.maxSets = kMaxFrames * kMaxMips;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &pci, nullptr, &pool_) != VK_SUCCESS)
        return false;

    ready_ = false;
    return true;
}

bool HzbPyramid::create_pipelines(VkDevice device) {
    VkShaderModule copy_mod = load_module(device, "shaders/hzb_copy.comp.spv");
    VkShaderModule reduce_mod = load_module(device, "shaders/hzb_reduce.comp.spv");
    if (!copy_mod || !reduce_mod) {
        LOG_ERROR("[Hzb] Failed to load hzb_copy / hzb_reduce SPIR-V");
        if (copy_mod) vkDestroyShaderModule(device, copy_mod, nullptr);
        if (reduce_mod) vkDestroyShaderModule(device, reduce_mod, nullptr);
        return false;
    }

    auto make = [&](VkShaderModule mod, VkPipeline* out) {
        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = mod;
        stage.pName = "main";
        VkComputePipelineCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        ci.stage = stage;
        ci.layout = pipeline_layout_;
        return vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, out) ==
               VK_SUCCESS;
    };

    bool ok = make(copy_mod, &copy_pipeline_) && make(reduce_mod, &reduce_pipeline_);

    vkDestroyShaderModule(device, copy_mod, nullptr);
    vkDestroyShaderModule(device, reduce_mod, nullptr);
    return ok;
}

void HzbPyramid::destroy_images(VkDevice device, VmaAllocator allocator, uint32_t frame) {
    if (full_views_[frame] != VK_NULL_HANDLE) {
        vkDestroyImageView(device, full_views_[frame], nullptr);
        full_views_[frame] = VK_NULL_HANDLE;
    }
    for (VkImageView v : mip_views_[frame]) {
        if (v != VK_NULL_HANDLE)
            vkDestroyImageView(device, v, nullptr);
    }
    mip_views_[frame].clear();

    if (images_[frame].handle != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, images_[frame].handle, images_[frame].allocation);
        images_[frame] = {};
    }
}

bool HzbPyramid::create_images(VkDevice device, VmaAllocator allocator, uint32_t frame) {
    destroy_images(device, allocator, frame);

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R32G32_SFLOAT; // R=min, G=max
    ici.extent = {width_, height_, 1};
    ici.mipLevels = mip_count_;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &ici, &aci, &images_[frame].handle,
                       &images_[frame].allocation, &images_[frame].info) != VK_SUCCESS) {
        LOG_ERROR("[Hzb] Failed to create pyramid image");
        return false;
    }

    mip_views_[frame].resize(mip_count_);
    for (uint32_t m = 0; m < mip_count_; ++m) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = images_[frame].handle;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_R32G32_SFLOAT;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel = m;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vci, nullptr, &mip_views_[frame][m]) != VK_SUCCESS)
            return false;
    }

    VkImageViewCreateInfo full{};
    full.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    full.image = images_[frame].handle;
    full.viewType = VK_IMAGE_VIEW_TYPE_2D;
    full.format = VK_FORMAT_R32G32_SFLOAT;
    full.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    full.subresourceRange.baseMipLevel = 0;
    full.subresourceRange.levelCount = mip_count_;
    full.subresourceRange.baseArrayLayer = 0;
    full.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &full, nullptr, &full_views_[frame]) != VK_SUCCESS)
        return false;

    // 1 copy + (mip_count-1) reduce
    const uint32_t nsets = mip_count_;
    std::vector<VkDescriptorSetLayout> layouts(nsets, set_layout_);
    std::vector<VkDescriptorSet> sets(nsets);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = nsets;
    ai.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &ai, sets.data()) != VK_SUCCESS) {
        LOG_ERROR("[Hzb] Failed to allocate descriptor sets");
        return false;
    }
    copy_sets_[frame] = sets[0];
    reduce_sets_[frame].assign(sets.begin() + 1, sets.end());

    // Pre-write reduce sets (stable mip views). Copy set: mip0 storage now;
    // depth source is filled by bind_depth_source() when prepass depth exists.
    {
        VkDescriptorImageInfo mip0_info{};
        mip0_info.imageView = mip_views_[frame][0];
        mip0_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = copy_sets_[frame];
        w.dstBinding = 1;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        w.pImageInfo = &mip0_info;
        vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    }
    for (uint32_t m = 1; m < mip_count_; ++m) {
        const uint32_t ri = m - 1;
        VkDescriptorImageInfo src_info{};
        src_info.sampler = sampler_;
        src_info.imageView = mip_views_[frame][m - 1];
        src_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo dst_info{};
        dst_info.imageView = mip_views_[frame][m];
        dst_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = reduce_sets_[frame][ri];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &src_info;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = reduce_sets_[frame][ri];
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].pImageInfo = &dst_info;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }

    return true;
}

void HzbPyramid::bind_depth_source(uint32_t frame_index, VkImageView depth_view) {
    if (!ready_ || frame_index >= kMaxFrames || depth_view == VK_NULL_HANDLE ||
        copy_sets_[frame_index] == VK_NULL_HANDLE)
        return;

    VkDescriptorImageInfo depth_info{};
    depth_info.sampler = depth_sampler_;
    depth_info.imageView = depth_view;
    depth_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = copy_sets_[frame_index];
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &depth_info;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

bool HzbPyramid::resize(VkDevice device, VmaAllocator allocator, VkExtent2D extent) {
    if (extent.width == 0 || extent.height == 0)
        return false;

    // Half-res pyramid; mip0 is min-downsampled from full-res depth (hzb_copy.comp).
    width_ = std::max(1u, extent.width / 2);
    height_ = std::max(1u, extent.height / 2);
    mip_count_ = calc_mip_count(width_, height_);

    if (pool_ != VK_NULL_HANDLE) {
        vkResetDescriptorPool(device, pool_, 0);
    }
    for (uint32_t f = 0; f < kMaxFrames; ++f) {
        copy_sets_[f] = VK_NULL_HANDLE;
        reduce_sets_[f].clear();
        if (!create_images(device, allocator, f)) {
            ready_ = false;
            return false;
        }
    }

    ready_ = true;
    needs_layout_init_ = true;
    LOG_INFO("[Hzb] Pyramid " << width_ << "x" << height_ << " (" << mip_count_
             << " mips), same-frame double-buffered");
    return true;
}

void HzbPyramid::destroy(VkDevice device, VmaAllocator allocator) {
    for (uint32_t f = 0; f < kMaxFrames; ++f)
        destroy_images(device, allocator, f);

    if (copy_pipeline_)
        vkDestroyPipeline(device, copy_pipeline_, nullptr);
    if (reduce_pipeline_)
        vkDestroyPipeline(device, reduce_pipeline_, nullptr);
    if (pipeline_layout_)
        vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (set_layout_)
        vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
    if (pool_)
        vkDestroyDescriptorPool(device, pool_, nullptr);
    if (sampler_)
        vkDestroySampler(device, sampler_, nullptr);
    if (depth_sampler_)
        vkDestroySampler(device, depth_sampler_, nullptr);

    copy_pipeline_ = VK_NULL_HANDLE;
    reduce_pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    depth_sampler_ = VK_NULL_HANDLE;
    ready_ = false;
    device_ = VK_NULL_HANDLE;
}

void HzbPyramid::record_init_layouts(VkCommandBuffer cmd) {
    if (!ready_ || !needs_layout_init_)
        return;
    for (uint32_t f = 0; f < kMaxFrames; ++f) {
        if (images_[f].handle == VK_NULL_HANDLE)
            continue;
        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = images_[f].handle;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_count_, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &bar);
    }
    needs_layout_init_ = false;
}

void HzbPyramid::record_build(VkCommandBuffer cmd, uint32_t frame_index,
                              VkExtent2D depth_extent) {
    if (!ready_ || frame_index >= kMaxFrames || copy_sets_[frame_index] == VK_NULL_HANDLE)
        return;
    if (depth_extent.width == 0 || depth_extent.height == 0)
        return;

    // Pyramid lives in GENERAL (init + cull sample). Barrier for storage overwrite.
    // Do NOT use UNDEFINED here: the image is already bound as GENERAL on the cull
    // set for this frame (frustum pass) and mid-CB discard confuses validation.
    {
        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = images_[frame_index].handle;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_count_, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &bar);
    }

    // Descriptors are pre-written (bind_depth_source + create_images) — no
    // vkUpdateDescriptorSets here (would invalidate the recording command buffer).
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, copy_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1,
                            &copy_sets_[frame_index], 0, nullptr);
    uint32_t pc_copy[4] = {width_, height_, depth_extent.width, depth_extent.height};
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(pc_copy), pc_copy);
    vkCmdDispatch(cmd, (width_ + 7) / 8, (height_ + 7) / 8, 1);

    uint32_t src_w = width_;
    uint32_t src_h = height_;
    for (uint32_t m = 1; m < mip_count_; ++m) {
        const uint32_t dst_w = std::max(1u, src_w / 2);
        const uint32_t dst_h = std::max(1u, src_h / 2);

        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = images_[frame_index].handle;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &bar);

        const uint32_t ri = m - 1;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, reduce_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0,
                                1, &reduce_sets_[frame_index][ri], 0, nullptr);
        uint32_t pc_red[4] = {src_w, src_h, dst_w, dst_h};
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(pc_red), pc_red);
        vkCmdDispatch(cmd, (dst_w + 7) / 8, (dst_h + 7) / 8, 1);

        src_w = dst_w;
        src_h = dst_h;
    }

    // HZB ready for same-frame occlusion cull — stay GENERAL (matches bind_hzb).
    {
        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = images_[frame_index].handle;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mip_count_, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &bar);
    }
}

} // namespace gfx
