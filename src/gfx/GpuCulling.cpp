#include "gfx/GpuCulling.h"
#include "gfx/BufferUtils.h"
#include "gfx/MeshData.h"
#include "gfx/ShaderLoader.h"
#include "scene/SceneManager.h"
#include "core/AABB.h"
#include "core/Frustum.h"
#include "core/Log.h"

#include <algorithm>
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
    ici.format = VK_FORMAT_R32G32_SFLOAT;
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
    vci.format = VK_FORMAT_R32G32_SFLOAT;
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
    dummy_layout_ready_ = false;

    if (cull_pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device, cull_pipeline_, nullptr);
    if (build_pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device, build_pipeline_, nullptr);
    if (meshlet_pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device, meshlet_pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (set_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
    if (pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, pool_, nullptr);

    cull_pipeline_ = VK_NULL_HANDLE;
    build_pipeline_ = VK_NULL_HANDLE;
    meshlet_pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    sets_ = {};
    ready_ = false;
    device_ = VK_NULL_HANDLE;
}

void GpuCulling::clear_scene(VkDevice device, VmaAllocator allocator) {
    BufferUtils::destroy_buffer(device, allocator, batch_metas_);
    BufferUtils::destroy_buffer(device, allocator, meshlets_);
    for (uint32_t i = 0; i < kMaxFrames; ++i) {
        BufferUtils::destroy_buffer(device, allocator, cull_items_[i]);
        BufferUtils::destroy_buffer(device, allocator, worlds_[i]);
        BufferUtils::destroy_buffer(device, allocator, out_instances_[i]);
        for (uint32_t p = 0; p < kCullPassCount; ++p) {
            BufferUtils::destroy_buffer(device, allocator, cull_globals_[i][p]);
            BufferUtils::destroy_buffer(device, allocator, batch_counts_[i][p]);
            BufferUtils::destroy_buffer(device, allocator, indirect_cmds_[i][p]);
            BufferUtils::destroy_buffer(device, allocator, meshlet_cmds_[i][p]);
            BufferUtils::destroy_buffer(device, allocator, meshlet_draw_count_[i][p]);
        }
    }
    item_transform_indices_.clear();
    cpu_items_.clear();
    cpu_metas_.clear();
    item_count_ = 0;
    transparent_item_count_ = 0;
    batch_count_ = 0;
    instance_slot_count_ = 0;
    world_count_ = 0;
    max_meshlet_draws_ = 0;
    max_meshlets_per_batch_ = 0;
    has_transparent_half_ = false;
    meshlet_draw_ = {};
    ready_ = false;
}

bool GpuCulling::create_descriptors(VkDevice device) {
    VkDescriptorSetLayoutBinding b[11]{};
    for (uint32_t i = 0; i < 11; ++i) {
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
    b[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;      // worlds[]
    b[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;      // meshlets
    b[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;      // meshlet cmds
    b[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;     // meshlet draw count

    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 11;
    lci.pBindings = b;
    if (vkCreateDescriptorSetLayout(device, &lci, nullptr, &set_layout_) != VK_SUCCESS)
        return false;

    const uint32_t set_count = kMaxFrames * kCullPassCount;
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, set_count},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, set_count * 9},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, set_count},
    };
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = set_count;
    pci.poolSizeCount = 3;
    pci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &pci, nullptr, &pool_) != VK_SUCCESS)
        return false;

    for (uint32_t i = 0; i < kMaxFrames; ++i) {
        for (uint32_t p = 0; p < kCullPassCount; ++p) {
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = pool_;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &set_layout_;
            if (vkAllocateDescriptorSets(device, &ai, &sets_[i][p]) != VK_SUCCESS)
                return false;
        }
    }
    return true;
}

bool GpuCulling::create_pipelines(VkDevice device) {
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(MeshletCullPush);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &set_layout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &pipeline_layout_) != VK_SUCCESS)
        return false;

    VkShaderModule cull_mod = load_module(device, "shaders/cull_frustum.comp.spv");
    VkShaderModule build_mod = load_module(device, "shaders/build_indirect.comp.spv");
    VkShaderModule meshlet_mod = load_module(device, "shaders/cull_meshlets.comp.spv");
    if (cull_mod == VK_NULL_HANDLE || build_mod == VK_NULL_HANDLE ||
        meshlet_mod == VK_NULL_HANDLE) {
        LOG_ERROR("[GpuCulling] Failed to load compute SPIR-V "
                  "(cull_frustum / build_indirect / cull_meshlets)");
        if (cull_mod) vkDestroyShaderModule(device, cull_mod, nullptr);
        if (build_mod) vkDestroyShaderModule(device, build_mod, nullptr);
        if (meshlet_mod) vkDestroyShaderModule(device, meshlet_mod, nullptr);
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

    bool ok = make_pipe(cull_mod, &cull_pipeline_) &&
              make_pipe(build_mod, &build_pipeline_) &&
              make_pipe(meshlet_mod, &meshlet_pipeline_);
    vkDestroyShaderModule(device, cull_mod, nullptr);
    vkDestroyShaderModule(device, build_mod, nullptr);
    vkDestroyShaderModule(device, meshlet_mod, nullptr);
    return ok;
}

bool GpuCulling::build_scene(VkDevice device, VmaAllocator allocator,
                             const std::vector<MeshDrawInfo>& mesh_draw_infos,
                             const scene::SceneManager& scene,
                             const MaterialManager* materials,
                             bool gpu_only_instances,
                             const std::vector<MeshletDesc>* meshlets) {
    clear_scene(device, allocator);

    batch_count_ = static_cast<uint32_t>(mesh_draw_infos.size());
    if (batch_count_ == 0) {
        ready_ = false;
        return true;
    }

    std::vector<GpuBatchMeta> metas(batch_count_);
    cpu_items_.clear();
    item_transform_indices_.clear();
    max_meshlet_draws_ = 0;
    max_meshlets_per_batch_ = 0;

    uint32_t running_base = 0;
    for (uint32_t b = 0; b < batch_count_; ++b) {
        const auto& info = mesh_draw_infos[b];
        const uint32_t cap = static_cast<uint32_t>(info.render_mesh_ids.size());
        metas[b].base = running_base;
        metas[b].capacity = cap;
        metas[b].index_count = info.index_count;
        metas[b].first_index = info.index_offset;
        metas[b].vertex_offset = info.vertex_offset;
        metas[b].meshlet_offset = info.meshlet_offset;
        metas[b].meshlet_count = info.meshlet_count;
        max_meshlets_per_batch_ =
            std::max(max_meshlets_per_batch_, info.meshlet_count);
        const uint32_t per_instance =
            (info.meshlet_count > 0) ? info.meshlet_count : 1u;
        max_meshlet_draws_ += per_instance * cap;

        for (uint32_t rm_id : info.render_mesh_ids) {
            const auto& rm = scene.get_render_mesh(rm_id);
            GpuCullItem item{};
            // Skinned meshes: inflate local AABB for cull (bind-pose + motion).
            core::AABB aabb = rm.local_aabb;
            if (rm.skin_index != ~0u && aabb.is_valid()) {
                const glm::vec3 c = aabb.center();
                const glm::vec3 e = aabb.extents() * 0.5f * 2.5f; // generous pad
                aabb.min = c - e;
                aabb.max = c + e;
            }
            item.aabb_min = glm::vec4(aabb.min, 0.0f);
            item.aabb_max = glm::vec4(aabb.max, 0.0f);
            item.meta = glm::uvec4(rm.material_index, b, running_base, cap);
            uint32_t joint_base = 0;
            uint32_t joint_count = 0;
            if (rm.skin_index != ~0u &&
                rm.skin_index < scene.skins().skin_count()) {
                const auto& sk = scene.skins().skin(rm.skin_index);
                joint_base = sk.palette_offset;
                joint_count = static_cast<uint32_t>(sk.joint_transform_indices.size());
            }
            // Material flags for depth/Hi-Z: only opaque (non-blend, non-transmission)
            // writers should build the pyramid. MASK still writes after alpha test.
            uint32_t mat_flags = 0;
            if (materials)
                mat_flags = materials->get_material_flags(rm.material_index);
            const uint32_t kBlend = Material::kFlagAlphaBlend;
            const uint32_t kTrans = Material::kFlagTransmission;
            item.skin = glm::uvec4(joint_base, joint_count, mat_flags,
                                   rm.transform_index);
            cpu_items_.push_back(item);
            item_transform_indices_.push_back(rm.transform_index);
        }
        running_base += cap;
    }
    item_count_ = static_cast<uint32_t>(cpu_items_.size());
    transparent_item_count_ = 0;
    const uint32_t kBlend = Material::kFlagAlphaBlend;
    const uint32_t kTrans = Material::kFlagTransmission;
    for (const auto& item : cpu_items_) {
        if ((item.skin.z & (kBlend | kTrans)) != 0u)
            ++transparent_item_count_;
    }

    instance_slot_count_ = running_base;
    has_transparent_half_ = transparent_item_count_ > 0;
    world_count_ = std::max(1u, scene.transforms().count());
    const uint32_t instance_halves = has_transparent_half_ ? kCullPassCount : 1u;
    const VkDeviceSize items_bytes =
        std::max<VkDeviceSize>(sizeof(GpuCullItem),
                               cpu_items_.size() * sizeof(GpuCullItem));
    const VkDeviceSize worlds_bytes =
        static_cast<VkDeviceSize>(world_count_) * sizeof(glm::mat4);
    const VkDeviceSize metas_bytes =
        std::max<VkDeviceSize>(sizeof(GpuBatchMeta), metas.size() * sizeof(GpuBatchMeta));
    const VkDeviceSize counts_bytes =
        std::max<VkDeviceSize>(sizeof(uint32_t), batch_count_ * sizeof(uint32_t));
    // Combined SSBO: opaque [0, N); transparent [N, 2N) only if the scene needs it.
    const VkDeviceSize out_bytes = std::max<VkDeviceSize>(
        sizeof(DrawInstanceGPU),
        static_cast<VkDeviceSize>(instance_halves) * running_base *
            sizeof(DrawInstanceGPU));
    const VkDeviceSize cmds_bytes =
        std::max<VkDeviceSize>(sizeof(uint32_t) * 5,
                               batch_count_ * 5 * sizeof(uint32_t));
    const uint32_t meshlet_n =
        (meshlets && !meshlets->empty()) ? static_cast<uint32_t>(meshlets->size())
                                         : 1u;
    const VkDeviceSize meshlets_bytes = std::max<VkDeviceSize>(
        sizeof(MeshletDesc),
        static_cast<VkDeviceSize>(meshlet_n) * sizeof(MeshletDesc));
    const uint32_t ml_draws = std::max(1u, max_meshlet_draws_);
    const VkDeviceSize ml_cmds_bytes =
        static_cast<VkDeviceSize>(ml_draws) * 5u * sizeof(uint32_t);

    if (!BufferUtils::initialize_buffer(device, allocator, metas_bytes, batch_metas_)) {
        LOG_ERROR("[GpuCulling] Failed to create batch meta buffer");
        return false;
    }
    memcpy(batch_metas_.mapped_data, metas.data(), metas.size() * sizeof(GpuBatchMeta));
    cpu_metas_ = metas;

    if (!BufferUtils::initialize_buffer(device, allocator, meshlets_bytes, meshlets_)) {
        LOG_ERROR("[GpuCulling] Failed to create meshlet buffer");
        return false;
    }
    if (meshlets && !meshlets->empty()) {
        memcpy(meshlets_.mapped_data, meshlets->data(),
               meshlets->size() * sizeof(MeshletDesc));
    } else {
        memset(meshlets_.mapped_data, 0, static_cast<size_t>(meshlets_bytes));
    }

    for (uint32_t f = 0; f < kMaxFrames; ++f) {
        if (!BufferUtils::initialize_buffer(device, allocator, items_bytes,
                                            cull_items_[f]) ||
            !BufferUtils::initialize_buffer(device, allocator, worlds_bytes,
                                            worlds_[f]) ||
            !BufferUtils::initialize_buffer(
                device, allocator, out_bytes, out_instances_[f],
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                gpu_only_instances ? BufferUtils::BufferResidency::GpuOnly
                                   : BufferUtils::BufferResidency::HostWrite)) {
            LOG_ERROR("[GpuCulling] Failed to create per-frame cull item/instance buffers");
            return false;
        }
        if (!cpu_items_.empty()) {
            memcpy(cull_items_[f].mapped_data, cpu_items_.data(),
                   cpu_items_.size() * sizeof(GpuCullItem));
        }
        {
            auto* dst = static_cast<glm::mat4*>(worlds_[f].mapped_data);
            const auto& xforms = scene.transforms();
            const uint32_t n = std::min(world_count_, xforms.count());
            for (uint32_t i = 0; i < n; ++i)
                dst[i] = xforms.get_world_matrix(i);
            for (uint32_t i = n; i < world_count_; ++i)
                dst[i] = glm::mat4(1.0f);
        }

        for (uint32_t p = 0; p < kCullPassCount; ++p) {
            if (!BufferUtils::initialize_buffer(device, allocator, sizeof(GpuCullGlobals),
                                                cull_globals_[f][p],
                                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ||
                !BufferUtils::initialize_buffer(device, allocator, counts_bytes,
                                                batch_counts_[f][p],
                                                VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
                !BufferUtils::initialize_buffer(
                    device, allocator, cmds_bytes, indirect_cmds_[f][p],
                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                    BufferUtils::BufferResidency::GpuOnly) ||
                !BufferUtils::initialize_buffer(
                    device, allocator, ml_cmds_bytes, meshlet_cmds_[f][p],
                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                    BufferUtils::BufferResidency::GpuOnly) ||
                !BufferUtils::initialize_buffer(
                    device, allocator, sizeof(uint32_t), meshlet_draw_count_[f][p],
                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    BufferUtils::BufferResidency::HostWrite)) {
                LOG_ERROR("[GpuCulling] Failed to create per-frame cull pass buffers");
                return false;
            }

            VkDescriptorBufferInfo infos[6]{};
            infos[0] = {cull_globals_[f][p].buffer, 0, sizeof(GpuCullGlobals)};
            infos[1] = {cull_items_[f].buffer, 0, items_bytes};
            infos[2] = {out_instances_[f].buffer, 0, out_bytes};
            infos[3] = {batch_counts_[f][p].buffer, 0, counts_bytes};
            infos[4] = {batch_metas_.buffer, 0, metas_bytes};
            infos[5] = {indirect_cmds_[f][p].buffer, 0, cmds_bytes};

            VkDescriptorImageInfo hzb_info{};
            hzb_info.sampler = dummy_sampler_;
            hzb_info.imageView = dummy_hzb_.view;
            hzb_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorBufferInfo worlds_info{worlds_[f].buffer, 0, worlds_bytes};
            VkDescriptorBufferInfo meshlets_info{meshlets_.buffer, 0, meshlets_bytes};
            VkDescriptorBufferInfo ml_cmds_info{meshlet_cmds_[f][p].buffer, 0,
                                                ml_cmds_bytes};
            VkDescriptorBufferInfo ml_count_info{meshlet_draw_count_[f][p].buffer, 0,
                                                 sizeof(uint32_t)};

            VkWriteDescriptorSet writes[11]{};
            for (uint32_t i = 0; i < 6; ++i) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = sets_[f][p];
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].pBufferInfo = &infos[i];
                writes[i].descriptorType =
                    (i == 0) ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                             : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            }
            writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[6].dstSet = sets_[f][p];
            writes[6].dstBinding = 6;
            writes[6].descriptorCount = 1;
            writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[6].pImageInfo = &hzb_info;
            writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[7].dstSet = sets_[f][p];
            writes[7].dstBinding = 7;
            writes[7].descriptorCount = 1;
            writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[7].pBufferInfo = &worlds_info;
            writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[8].dstSet = sets_[f][p];
            writes[8].dstBinding = 8;
            writes[8].descriptorCount = 1;
            writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[8].pBufferInfo = &meshlets_info;
            writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[9].dstSet = sets_[f][p];
            writes[9].dstBinding = 9;
            writes[9].descriptorCount = 1;
            writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[9].pBufferInfo = &ml_cmds_info;
            writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[10].dstSet = sets_[f][p];
            writes[10].dstBinding = 10;
            writes[10].descriptorCount = 1;
            writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[10].pBufferInfo = &ml_count_info;
            vkUpdateDescriptorSets(device, 11, writes, 0, nullptr);
        }
    }

    ready_ = true;
    LOG_INFO("[GpuCulling] Ready: " << item_count_ << " items, " << batch_count_
             << " batches, " << instance_slot_count_ << " instance slots"
             << (has_transparent_half_ ? " + transparent half" : "")
             << ", worlds=" << world_count_ << ", meshletCull="
             << (meshlet_cull_ ? "on" : "off") << " maxDraws=" << max_meshlet_draws_);
    return true;
}

void GpuCulling::update_models(uint32_t frame_index, const scene::SceneManager& scene) {
    if (!ready_ || frame_index >= kMaxFrames || !worlds_[frame_index].mapped_data)
        return;
    auto* dst = static_cast<glm::mat4*>(worlds_[frame_index].mapped_data);
    const auto& xforms = scene.transforms();
    const uint32_t n = std::min(world_count_, xforms.count());
    for (uint32_t i = 0; i < n; ++i)
        dst[i] = xforms.get_world_matrix(i);
}

void GpuCulling::bind_hzb(uint32_t frame_index, VkImageView hzb_view,
                          VkSampler hzb_sampler) {
    if (frame_index >= kMaxFrames)
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

    for (uint32_t p = 0; p < kCullPassCount; ++p) {
        if (sets_[frame_index][p] == VK_NULL_HANDLE)
            continue;
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = sets_[frame_index][p];
        w.dstBinding = 6;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &hzb_info;
        vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    }
}

void GpuCulling::record(VkCommandBuffer cmd, uint32_t frame_index,
                        const glm::mat4& view_proj, bool enable_hzb, uint32_t hzb_width,
                        uint32_t hzb_height, uint32_t hzb_mips, float hzb_depth_bias,
                        CullEmitFilter emit_filter, const glm::vec3& camera_world,
                        bool cone_cull, bool expand_meshlets) {
    if (!ready_ || frame_index >= kMaxFrames)
        return;

    const uint32_t pass = static_cast<uint32_t>(pass_for_filter(emit_filter));
    if (pass == static_cast<uint32_t>(CullPass::Transparent) && !has_transparent_half_)
        return;
    const bool use_hzb =
        enable_hzb && hzb_width > 0 && hzb_height > 0 && hzb_mips > 0;
    const uint32_t inst_offset =
        (pass == static_cast<uint32_t>(CullPass::Transparent)) ? instance_slot_count_
                                                               : 0u;

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
    g.hzb_info = glm::vec4(float(hzb_width), float(hzb_height), bias, 0.0f);
    g.emit_filter = static_cast<uint32_t>(emit_filter);
    g.instance_base_offset = inst_offset;
    g.pad1 = g.pad2 = 0;
    memcpy(cull_globals_[frame_index][pass].mapped_data, &g, sizeof(g));

    // Host write of globals → compute/transfer.
    VkMemoryBarrier host_barrier{};
    host_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    host_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    host_barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT |
                                 VK_ACCESS_TRANSFER_WRITE_BIT |
                                 VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 1, &host_barrier, 0, nullptr, 0, nullptr);

    // GPU-zero batch counts (host memset during *recording* races with multi-cull
    // in one CB — prepass then shade would see non-zero counts).
    if (batch_counts_[frame_index][pass].buffer != VK_NULL_HANDLE && batch_count_ > 0) {
        // Prior compute may have written counts (e.g. prepass cull → shade cull).
        VkMemoryBarrier to_fill{};
        to_fill.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        to_fill.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        to_fill.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &to_fill, 0, nullptr, 0,
                             nullptr);

        const VkDeviceSize counts_bytes =
            static_cast<VkDeviceSize>(batch_count_) * sizeof(uint32_t);
        vkCmdFillBuffer(cmd, batch_counts_[frame_index][pass].buffer, 0, counts_bytes, 0);

        VkMemoryBarrier fill_done{};
        fill_done.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        fill_done.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        fill_done.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fill_done, 0,
                             nullptr, 0, nullptr);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cull_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1,
                            &sets_[frame_index][pass], 0, nullptr);

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

    const bool use_meshlets = expand_meshlets && meshlet_cull_ &&
                              max_meshlet_draws_ > 0 &&
                              meshlet_pipeline_ != VK_NULL_HANDLE &&
                              meshlet_cmds_[frame_index][pass].buffer != VK_NULL_HANDLE;
    meshlet_draw_[frame_index][pass] = use_meshlets;

    if (use_meshlets) {
        VkMemoryBarrier to_fill_ml{};
        to_fill_ml.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        to_fill_ml.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        to_fill_ml.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &to_fill_ml, 0,
                             nullptr, 0, nullptr);
        vkCmdFillBuffer(cmd, meshlet_draw_count_[frame_index][pass].buffer, 0,
                        sizeof(uint32_t), 0);
        VkMemoryBarrier fill_ml{};
        fill_ml.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        fill_ml.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        fill_ml.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT |
                                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fill_ml, 0,
                             nullptr, 0, nullptr);

        MeshletCullPush pc{};
        pc.camera_world = glm::vec4(camera_world, 0.0f);
        pc.max_draws = max_meshlet_draws_;
        pc.cone_enable = cone_cull ? 1u : 0u;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, meshlet_pipeline_);
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(MeshletCullPush), &pc);
        const uint32_t mx = std::max(1u, max_meshlets_per_batch_);
        const uint32_t groups_x = (mx + 63u) / 64u;
        vkCmdDispatch(cmd, groups_x, batch_count_, 1);
    } else {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, build_pipeline_);
        const uint32_t bgroups = (batch_count_ + 63u) / 64u;
        if (bgroups > 0)
            vkCmdDispatch(cmd, bgroups, 1, 1);
    }

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

void GpuCulling::cmd_draw_indexed(VkCommandBuffer cmd, uint32_t frame_index,
                                  CullPass pass) const {
    if (!ready_ || frame_index >= kMaxFrames)
        return;
    const uint32_t pi = static_cast<uint32_t>(pass);
    if (pi >= kCullPassCount)
        return;
    if (pass == CullPass::Transparent && !has_transparent_half_)
        return;

    if (meshlet_draw_[frame_index][pi] &&
        meshlet_cmds_[frame_index][pi].buffer != VK_NULL_HANDLE &&
        meshlet_draw_count_[frame_index][pi].buffer != VK_NULL_HANDLE) {
        vkCmdDrawIndexedIndirectCount(cmd, meshlet_cmds_[frame_index][pi].buffer, 0,
                                      meshlet_draw_count_[frame_index][pi].buffer, 0,
                                      max_meshlet_draws_,
                                      sizeof(VkDrawIndexedIndirectCommand));
        return;
    }
    if (batch_count_ == 0)
        return;
    vkCmdDrawIndexedIndirect(cmd, indirect_cmds_[frame_index][pi].buffer, 0,
                             batch_count_, sizeof(VkDrawIndexedIndirectCommand));
}

GpuCulling::MeshletStats GpuCulling::read_meshlet_stats(uint32_t frame_index) const {
    MeshletStats out{};
    if (!ready_ || frame_index >= kMaxFrames || !meshlet_cull_ ||
        max_meshlet_draws_ == 0)
        return out;

    const uint32_t pass_n = has_transparent_half_ ? kCullPassCount : 1u;
    bool any = false;
    uint32_t fallback_draws = 0;
    uint32_t tested = 0;
    uint32_t drawn = 0;

    for (uint32_t p = 0; p < pass_n; ++p) {
        if (!meshlet_draw_[frame_index][p])
            continue;
        any = true;
        if (meshlet_draw_count_[frame_index][p].mapped_data) {
            drawn += *static_cast<const uint32_t*>(
                meshlet_draw_count_[frame_index][p].mapped_data);
        }
        if (!batch_counts_[frame_index][p].mapped_data)
            continue;
        const auto* counts = static_cast<const uint32_t*>(
            batch_counts_[frame_index][p].mapped_data);
        const uint32_t n = std::min(batch_count_, static_cast<uint32_t>(cpu_metas_.size()));
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t vis = std::min(counts[i], cpu_metas_[i].capacity);
            if (vis == 0)
                continue;
            if (cpu_metas_[i].meshlet_count == 0) {
                ++fallback_draws;
            } else {
                tested += vis * cpu_metas_[i].meshlet_count;
            }
        }
    }
    if (!any)
        return out;

    out.active = true;
    out.tested = tested;
    out.drawn = (drawn > fallback_draws) ? (drawn - fallback_draws) : 0u;
    if (out.drawn > out.tested)
        out.drawn = out.tested;
    return out;
}

uint32_t GpuCulling::read_visible_count(uint32_t frame_index) const {
    if (!ready_ || frame_index >= kMaxFrames)
        return 0;
    uint32_t sum = 0;
    const uint32_t pass_n = has_transparent_half_ ? kCullPassCount : 1u;
    for (uint32_t p = 0; p < pass_n; ++p) {
        if (!batch_counts_[frame_index][p].mapped_data)
            continue;
        const auto* counts =
            static_cast<const uint32_t*>(batch_counts_[frame_index][p].mapped_data);
        for (uint32_t i = 0; i < batch_count_; ++i)
            sum += counts[i];
    }
    return sum;
}

} // namespace gfx
