#include "gfx/GpuCulling.h"
#include "gfx/BufferUtils.h"
#include "gfx/ShaderLoader.h"
#include "scene/SceneManager.h"
#include "core/Frustum.h"
#include "core/Log.h"

#include <cstring>
#include <vector>

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

} // namespace

bool GpuCulling::initialize(VkDevice device, VmaAllocator allocator) {
    device_ = device;
    if (!create_descriptors(device))
        return false;
    if (!create_pipelines(device))
        return false;

    // 1x1 R32F dummy so binding 6 is always valid when HZB is off.
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R32_SFLOAT;
    ici.extent = {1, 1, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(allocator, &ici, &aci, &dummy_hzb_.handle, &dummy_hzb_.allocation,
                       &dummy_hzb_.info) != VK_SUCCESS) {
        LOG_ERROR("[GpuCulling] Failed to create dummy HZB image");
        return false;
    }
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = dummy_hzb_.handle;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R32_SFLOAT;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &vci, nullptr, &dummy_hzb_.view) != VK_SUCCESS)
        return false;

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &sci, nullptr, &dummy_sampler_) != VK_SUCCESS)
        return false;

    return true;
}

void GpuCulling::destroy(VkDevice device, VmaAllocator allocator) {
    clear_scene(device, allocator);

    if (dummy_hzb_.view)
        vkDestroyImageView(device, dummy_hzb_.view, nullptr);
    if (dummy_hzb_.handle)
        vmaDestroyImage(allocator, dummy_hzb_.handle, dummy_hzb_.allocation);
    dummy_hzb_ = {};
    if (dummy_sampler_)
        vkDestroySampler(device, dummy_sampler_, nullptr);
    dummy_sampler_ = VK_NULL_HANDLE;

    if (cull_pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device, cull_pipeline_, nullptr);
    if (build_pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device, build_pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (set_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
    if (pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, pool_, nullptr);

    cull_pipeline_ = VK_NULL_HANDLE;
    build_pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    sets_ = {};
    ready_ = false;
    device_ = VK_NULL_HANDLE;
}

void GpuCulling::clear_scene(VkDevice device, VmaAllocator allocator) {
    BufferUtils::destroy_buffer(device, allocator, cull_items_);
    BufferUtils::destroy_buffer(device, allocator, batch_metas_);
    for (uint32_t i = 0; i < kMaxFrames; ++i) {
        BufferUtils::destroy_buffer(device, allocator, cull_globals_[i]);
        BufferUtils::destroy_buffer(device, allocator, batch_counts_[i]);
        BufferUtils::destroy_buffer(device, allocator, out_instances_[i]);
        BufferUtils::destroy_buffer(device, allocator, indirect_cmds_[i]);
    }
    item_count_ = 0;
    batch_count_ = 0;
    ready_ = false;
}

bool GpuCulling::create_descriptors(VkDevice device) {
    VkDescriptorSetLayoutBinding b[7]{};
    for (uint32_t i = 0; i < 7; ++i) {
        b[i].binding = i;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;        // globals
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;      // cull items
    b[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;      // out instances
    b[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;      // batch counts
    b[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;      // batch metas
    b[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;      // indirect cmds
    b[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // HZB

    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 7;
    lci.pBindings = b;
    if (vkCreateDescriptorSetLayout(device, &lci, nullptr, &set_layout_) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFrames},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxFrames * 5},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFrames},
    };
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = kMaxFrames;
    pci.poolSizeCount = 3;
    pci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &pci, nullptr, &pool_) != VK_SUCCESS)
        return false;

    for (uint32_t i = 0; i < kMaxFrames; ++i) {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = pool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &set_layout_;
        if (vkAllocateDescriptorSets(device, &ai, &sets_[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

bool GpuCulling::create_pipelines(VkDevice device) {
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &set_layout_;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &pipeline_layout_) != VK_SUCCESS)
        return false;

    VkShaderModule cull_mod = load_module(device, "shaders/cull_frustum.comp.spv");
    VkShaderModule build_mod = load_module(device, "shaders/build_indirect.comp.spv");
    if (cull_mod == VK_NULL_HANDLE || build_mod == VK_NULL_HANDLE) {
        LOG_ERROR("[GpuCulling] Failed to load compute SPIR-V (cull_frustum / build_indirect)");
        if (cull_mod) vkDestroyShaderModule(device, cull_mod, nullptr);
        if (build_mod) vkDestroyShaderModule(device, build_mod, nullptr);
        return false;
    }

    auto make_pipe = [&](VkShaderModule mod, VkPipeline* out) -> bool {
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

    bool ok = make_pipe(cull_mod, &cull_pipeline_) && make_pipe(build_mod, &build_pipeline_);
    vkDestroyShaderModule(device, cull_mod, nullptr);
    vkDestroyShaderModule(device, build_mod, nullptr);
    return ok;
}

bool GpuCulling::build_scene(VkDevice device, VmaAllocator allocator,
                             const std::vector<MeshDrawInfo>& mesh_draw_infos,
                             const scene::SceneManager& scene) {
    clear_scene(device, allocator);

    batch_count_ = static_cast<uint32_t>(mesh_draw_infos.size());
    if (batch_count_ == 0) {
        ready_ = false;
        return true;
    }

    std::vector<GpuBatchMeta> metas(batch_count_);
    std::vector<GpuCullItem> items;

    uint32_t running_base = 0;
    for (uint32_t b = 0; b < batch_count_; ++b) {
        const auto& info = mesh_draw_infos[b];
        const uint32_t cap = static_cast<uint32_t>(info.render_mesh_ids.size());
        metas[b].base = running_base;
        metas[b].capacity = cap;
        metas[b].index_count = info.index_count;
        metas[b].first_index = info.index_offset;
        metas[b].vertex_offset = info.vertex_offset;

        for (uint32_t rm_id : info.render_mesh_ids) {
            const auto& rm = scene.get_render_mesh(rm_id);
            GpuCullItem item{};
            item.model = scene.transforms().get_world_matrix(rm.transform_index);
            item.aabb_min = glm::vec4(rm.local_aabb.min, 0.0f);
            item.aabb_max = glm::vec4(rm.local_aabb.max, 0.0f);
            item.meta = glm::uvec4(rm.material_index, b, running_base, cap);
            items.push_back(item);
        }
        running_base += cap;
    }
    item_count_ = static_cast<uint32_t>(items.size());

    const VkDeviceSize items_bytes =
        std::max<VkDeviceSize>(sizeof(GpuCullItem), items.size() * sizeof(GpuCullItem));
    const VkDeviceSize metas_bytes =
        std::max<VkDeviceSize>(sizeof(GpuBatchMeta), metas.size() * sizeof(GpuBatchMeta));
    const VkDeviceSize counts_bytes =
        std::max<VkDeviceSize>(sizeof(uint32_t), batch_count_ * sizeof(uint32_t));
    const VkDeviceSize out_bytes =
        std::max<VkDeviceSize>(sizeof(DrawInstanceGPU),
                               running_base * sizeof(DrawInstanceGPU));
    // 5 uints per indirect command
    const VkDeviceSize cmds_bytes =
        std::max<VkDeviceSize>(sizeof(uint32_t) * 5,
                               batch_count_ * 5 * sizeof(uint32_t));

    if (!BufferUtils::initialize_buffer(device, allocator, items_bytes, cull_items_) ||
        !BufferUtils::initialize_buffer(device, allocator, metas_bytes, batch_metas_)) {
        LOG_ERROR("[GpuCulling] Failed to create static cull buffers");
        return false;
    }
    memcpy(cull_items_.mapped_data, items.data(), items.size() * sizeof(GpuCullItem));
    memcpy(batch_metas_.mapped_data, metas.data(), metas.size() * sizeof(GpuBatchMeta));

    for (uint32_t f = 0; f < kMaxFrames; ++f) {
        if (!BufferUtils::initialize_buffer(device, allocator, sizeof(GpuCullGlobals),
                                            cull_globals_[f],
                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ||
            // TRANSFER_DST required for vkCmdFillBuffer zeroing each frame
            !BufferUtils::initialize_buffer(device, allocator, counts_bytes,
                                            batch_counts_[f],
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
            !BufferUtils::initialize_buffer(device, allocator, out_bytes,
                                            out_instances_[f]) ||
            !BufferUtils::initialize_buffer(device, allocator, cmds_bytes,
                                            indirect_cmds_[f],
                                            VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)) {
            LOG_ERROR("[GpuCulling] Failed to create per-frame cull buffers");
            return false;
        }

        // Buffer bindings are static for the scene. HZB image is set via bind_hzb
        // (dummy until the pyramid is wired after resize / scene load).
        VkDescriptorBufferInfo infos[6]{};
        infos[0] = {cull_globals_[f].buffer, 0, sizeof(GpuCullGlobals)};
        infos[1] = {cull_items_.buffer, 0, items_bytes};
        infos[2] = {out_instances_[f].buffer, 0, out_bytes};
        infos[3] = {batch_counts_[f].buffer, 0, counts_bytes};
        infos[4] = {batch_metas_.buffer, 0, metas_bytes};
        infos[5] = {indirect_cmds_[f].buffer, 0, cmds_bytes};

        VkDescriptorImageInfo hzb_info{};
        hzb_info.sampler = dummy_sampler_;
        hzb_info.imageView = dummy_hzb_.view;
        hzb_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkWriteDescriptorSet writes[7]{};
        for (uint32_t i = 0; i < 6; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = sets_[f];
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].pBufferInfo = &infos[i];
            writes[i].descriptorType =
                (i == 0) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                         : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = sets_[f];
        writes[6].dstBinding = 6;
        writes[6].descriptorCount = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[6].pImageInfo = &hzb_info;
        vkUpdateDescriptorSets(device, 7, writes, 0, nullptr);
    }

    ready_ = true;
    LOG_INFO("[GpuCulling] Ready: " << item_count_ << " items, " << batch_count_
             << " batches, " << running_base << " instance slots");
    return true;
}

void GpuCulling::bind_hzb(uint32_t frame_index, VkImageView hzb_view,
                          VkSampler hzb_sampler) {
    if (frame_index >= kMaxFrames || sets_[frame_index] == VK_NULL_HANDLE)
        return;

    VkDescriptorImageInfo hzb_info{};
    if (hzb_view != VK_NULL_HANDLE && hzb_sampler != VK_NULL_HANDLE) {
        hzb_info.sampler = hzb_sampler;
        hzb_info.imageView = hzb_view;
        // Pyramid is GENERAL for its whole life (init + build + sample). Avoids
        // SHADER_READ_ONLY vs UNDEFINED mismatches on the first frames.
        hzb_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    } else {
        hzb_info.sampler = dummy_sampler_;
        hzb_info.imageView = dummy_hzb_.view;
        hzb_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = sets_[frame_index];
    w.dstBinding = 6;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &hzb_info;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

void GpuCulling::record(VkCommandBuffer cmd, uint32_t frame_index,
                        const glm::mat4& view_proj, bool enable_hzb, uint32_t hzb_width,
                        uint32_t hzb_height, uint32_t hzb_mips, float hzb_depth_bias) {
    if (!ready_ || frame_index >= kMaxFrames)
        return;

    const bool use_hzb =
        enable_hzb && hzb_width > 0 && hzb_height > 0 && hzb_mips > 0;

    // BestPractices-ImageMemoryBarrier-TransitionUndefinedToReadOnly: never go
    // UNDEFINED → SHADER_READ_ONLY (discards into a read-only layout). GENERAL is fine
    // for the unused dummy; the shader does not sample it when hzb_enabled == 0.
    if (!dummy_layout_ready_ && dummy_hzb_.handle != VK_NULL_HANDLE) {
        VkImageMemoryBarrier bar{};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = dummy_hzb_.handle;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &bar);
        dummy_layout_ready_ = true;
    }

    // Same-frame: frustum + HZB use the same view_proj as the depth prepass.
    // HZB image descriptor is already set via bind_hzb (no mid-record updates).
    core::Frustum fr = core::Frustum::from_view_proj(view_proj);
    GpuCullGlobals g{};
    for (int i = 0; i < 6; ++i)
        g.planes[i] = fr.planes[i];
    g.item_count = item_count_;
    g.batch_count = batch_count_;
    g.hzb_enabled = use_hzb ? 1u : 0u;
    g.hzb_mips = hzb_mips;
    g.view_proj = view_proj;
    const float bias = (hzb_depth_bias > 0.0f) ? hzb_depth_bias : 0.003f;
    g.hzb_info = glm::vec4(float(hzb_width), float(hzb_height), bias, 1.0f);
    memcpy(cull_globals_[frame_index].mapped_data, &g, sizeof(g));

    // Zero batch counts on the host (buffer is persistently mapped + coherent).
    if (batch_counts_[frame_index].mapped_data && batch_count_ > 0) {
        memset(batch_counts_[frame_index].mapped_data, 0,
               batch_count_ * sizeof(uint32_t));
    }

    VkMemoryBarrier host_barrier{};
    host_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    host_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    host_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                 VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &host_barrier, 0,
                         nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cull_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1,
                            &sets_[frame_index], 0, nullptr);

    const uint32_t groups = (item_count_ + 63u) / 64u;
    if (groups > 0)
        vkCmdDispatch(cmd, groups, 1, 1);

    VkMemoryBarrier cull_barrier{};
    cull_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    cull_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    cull_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &cull_barrier, 0,
                         nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, build_pipeline_);
    // same descriptor set
    const uint32_t bgroups = (batch_count_ + 63u) / 64u;
    if (bgroups > 0)
        vkCmdDispatch(cmd, bgroups, 1, 1);

    VkMemoryBarrier to_draw{};
    to_draw.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    to_draw.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_draw.dstAccessMask =
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                         0, 1, &to_draw, 0, nullptr, 0, nullptr);
}

uint32_t GpuCulling::read_visible_count(uint32_t frame_index) const {
    if (!ready_ || frame_index >= kMaxFrames || !batch_counts_[frame_index].mapped_data)
        return 0;
    const auto* counts =
        static_cast<const uint32_t*>(batch_counts_[frame_index].mapped_data);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < batch_count_; ++i)
        sum += counts[i];
    return sum;
}

} // namespace gfx
