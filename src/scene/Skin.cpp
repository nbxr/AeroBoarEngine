#include "scene/Skin.h"
#include "gfx/BufferUtils.h"
#include "gfx/ShaderLoader.h"
#include "core/Log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tiny_gltf.h>

namespace scene {
namespace {

void read_mat4_accessor(const tinygltf::Model& model, int accessor_index,
                        std::vector<glm::mat4>& out) {
    out.clear();
    if (accessor_index < 0 ||
        accessor_index >= static_cast<int>(model.accessors.size()))
        return;
    const auto& acc = model.accessors[static_cast<size_t>(accessor_index)];
    if (acc.type != TINYGLTF_TYPE_MAT4 ||
        acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
        return;
    if (acc.bufferView < 0 ||
        acc.bufferView >= static_cast<int>(model.bufferViews.size()))
        return;
    const auto& bv = model.bufferViews[static_cast<size_t>(acc.bufferView)];
    if (bv.buffer < 0 || bv.buffer >= static_cast<int>(model.buffers.size()))
        return;
    const auto& buf = model.buffers[static_cast<size_t>(bv.buffer)];
    int stride = acc.ByteStride(bv);
    if (stride <= 0)
        stride = 64;

    const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
    out.resize(acc.count);
    for (size_t i = 0; i < acc.count; ++i) {
        const float* f =
            reinterpret_cast<const float*>(base + i * static_cast<size_t>(stride));
        out[i] = glm::make_mat4(f);
    }
}

} // namespace

void SkinSystem::clear() {
    skins_.clear();
    cpu_palette_.clear();
    total_joints_ = 0;
}

void SkinSystem::destroy(VkDevice device, VmaAllocator allocator) {
    destroy_compute(device, allocator);
    for (uint32_t f = 0; f < kMaxFrames; ++f)
        gfx::BufferUtils::destroy_buffer(device, allocator, joint_buffers_[f]);
    gfx::BufferUtils::destroy_buffer(device, allocator, ibm_buffer_);
    gfx::BufferUtils::destroy_buffer(device, allocator, meta_buffer_);
    gpu_ready_ = false;
    device_ = VK_NULL_HANDLE;
    clear();
}

uint32_t SkinSystem::load_from_gltf(
    const tinygltf::Model& model,
    const std::vector<uint32_t>& gltf_node_to_transform) {
    clear();
    // Preserve model.skins indices so node.skin maps 1:1 to skins_[i].
    skins_.resize(model.skins.size());
    total_joints_ = 0;
    uint32_t loaded = 0;

    for (size_t si = 0; si < model.skins.size(); ++si) {
        const auto& ts = model.skins[si];
        Skin skin{};
        skin.palette_offset = total_joints_;
        skin.joint_transform_indices.reserve(ts.joints.size());

        for (int jnode : ts.joints) {
            if (jnode < 0 ||
                static_cast<size_t>(jnode) >= gltf_node_to_transform.size()) {
                skin.joint_transform_indices.push_back(TransformManager::kInvalid);
                continue;
            }
            skin.joint_transform_indices.push_back(
                gltf_node_to_transform[static_cast<size_t>(jnode)]);
        }

        if (ts.inverseBindMatrices >= 0) {
            read_mat4_accessor(model, ts.inverseBindMatrices,
                               skin.inverse_bind_matrices);
        }
        if (skin.inverse_bind_matrices.size() < skin.joint_transform_indices.size()) {
            skin.inverse_bind_matrices.resize(skin.joint_transform_indices.size(),
                                              glm::mat4(1.0f));
        }

        if (skin.joint_transform_indices.empty()) {
            skins_[si] = Skin{}; // empty placeholder; joint_count 0 = rigid
            continue;
        }

        if (total_joints_ + skin.joint_transform_indices.size() > kMaxJointsTotal) {
            LOG_ERROR("[Skin] Exceeded kMaxJointsTotal (" << kMaxJointsTotal
                      << "); remaining skins empty");
            skins_[si] = Skin{};
            continue;
        }

        total_joints_ += static_cast<uint32_t>(skin.joint_transform_indices.size());
        skins_[si] = std::move(skin);
        ++loaded;
    }

    cpu_palette_.assign(std::max(1u, total_joints_), glm::mat4(1.0f));
    LOG_INFO("[Skin] Loaded " << loaded << " skin(s) (" << skins_.size()
             << " slots), " << total_joints_ << " joint(s)");
    return loaded;
}

void SkinSystem::set_mesh_transform(uint32_t skin_index,
                                    uint32_t mesh_transform_index) {
    if (skin_index >= skins_.size())
        return;
    // First mesh node that references this skin wins (typical assets: one mesh).
    if (skins_[skin_index].mesh_transform_index == TransformManager::kInvalid)
        skins_[skin_index].mesh_transform_index = mesh_transform_index;
}

bool SkinSystem::create_gpu_buffers(VkDevice device, VmaAllocator allocator) {
    destroy_compute(device, allocator);
    for (uint32_t f = 0; f < kMaxFrames; ++f)
        gfx::BufferUtils::destroy_buffer(device, allocator, joint_buffers_[f]);
    gfx::BufferUtils::destroy_buffer(device, allocator, ibm_buffer_);
    gfx::BufferUtils::destroy_buffer(device, allocator, meta_buffer_);

    device_ = device;
    const VkDeviceSize palette_bytes = joint_buffer_size();
    const uint32_t n = std::max(1u, total_joints_);

    for (uint32_t f = 0; f < kMaxFrames; ++f) {
        if (!gfx::BufferUtils::initialize_buffer(
                device, allocator, palette_bytes, joint_buffers_[f], 0,
                gfx::BufferUtils::BufferResidency::GpuOnly)) {
            LOG_ERROR("[Skin] Failed to create joint matrix buffer");
            gpu_ready_ = false;
            return false;
        }
    }

    std::vector<glm::mat4> ibm(n, glm::mat4(1.0f));
    std::vector<glm::uvec4> meta(n, glm::uvec4(~0u, ~0u, 0u, 0u));
    uint32_t cursor = 0;
    for (const Skin& skin : skins_) {
        for (size_t j = 0; j < skin.joint_transform_indices.size(); ++j) {
            if (cursor >= n)
                break;
            ibm[cursor] = (j < skin.inverse_bind_matrices.size())
                              ? skin.inverse_bind_matrices[j]
                              : glm::mat4(1.0f);
            meta[cursor] = glm::uvec4(skin.joint_transform_indices[j],
                                      skin.mesh_transform_index,
                                      skin.palette_offset + static_cast<uint32_t>(j),
                                      0u);
            ++cursor;
        }
    }

    const VkDeviceSize ibm_bytes = sizeof(glm::mat4) * n;
    const VkDeviceSize meta_bytes = sizeof(glm::uvec4) * n;
    if (!gfx::BufferUtils::initialize_buffer(device, allocator, ibm_bytes,
                                             ibm_buffer_) ||
        !gfx::BufferUtils::initialize_buffer(device, allocator, meta_bytes,
                                             meta_buffer_)) {
        LOG_ERROR("[Skin] Failed to create IBM/meta buffers");
        gpu_ready_ = false;
        return false;
    }
    std::memcpy(ibm_buffer_.mapped_data, ibm.data(),
                static_cast<size_t>(ibm_bytes));
    std::memcpy(meta_buffer_.mapped_data, meta.data(),
                static_cast<size_t>(meta_bytes));

    gpu_ready_ = true;
    if (total_joints_ == 0)
        return true;
    if (!create_compute(device)) {
        LOG_ERROR("[Skin] GPU palette compute failed — CPU fallback");
        destroy_compute(device, allocator);
        for (uint32_t f = 0; f < kMaxFrames; ++f) {
            gfx::BufferUtils::destroy_buffer(device, allocator, joint_buffers_[f]);
            if (!gfx::BufferUtils::initialize_buffer(device, allocator, palette_bytes,
                                                     joint_buffers_[f])) {
                gpu_ready_ = false;
                return false;
            }
            auto* dst = static_cast<glm::mat4*>(joint_buffers_[f].mapped_data);
            for (uint32_t i = 0; i < n; ++i)
                dst[i] = glm::mat4(1.0f);
        }
        return true;
    }
    LOG_INFO("[Skin] GPU palette compute ready (" << total_joints_ << " joints)");
    return true;
}

void SkinSystem::update_joint_matrices(uint32_t frame_index,
                                       const TransformManager& transforms) {
    if (!gpu_ready_ || frame_index >= kMaxFrames || total_joints_ == 0)
        return;

    for (const Skin& skin : skins_) {
        // glTF: jointMatrix = inv(meshWorld) * jointWorld * IBM
        // VS multiplies by mesh model → world = jointWorld * IBM * v
        glm::mat4 inv_mesh(1.0f);
        if (skin.mesh_transform_index != TransformManager::kInvalid &&
            transforms.is_alive(skin.mesh_transform_index)) {
            inv_mesh = glm::inverse(
                transforms.get_world_matrix(skin.mesh_transform_index));
        }

        for (size_t j = 0; j < skin.joint_transform_indices.size(); ++j) {
            const uint32_t xi = skin.joint_transform_indices[j];
            glm::mat4 joint_world(1.0f);
            if (xi != TransformManager::kInvalid && transforms.is_alive(xi))
                joint_world = transforms.get_world_matrix(xi);
            const glm::mat4& ibm = skin.inverse_bind_matrices[j];
            cpu_palette_[skin.palette_offset + static_cast<uint32_t>(j)] =
                inv_mesh * joint_world * ibm;
        }
    }

    auto* dst = static_cast<glm::mat4*>(joint_buffers_[frame_index].mapped_data);
    if (!dst)
        return;
    std::memcpy(dst, cpu_palette_.data(),
                sizeof(glm::mat4) * total_joints_);
}

void SkinSystem::destroy_compute(VkDevice device, VmaAllocator /*allocator*/) {
    if (palette_pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device, palette_pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (set_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
    if (pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, pool_, nullptr);
    palette_pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    sets_ = {};
}

bool SkinSystem::create_compute(VkDevice device) {
    VkDescriptorSetLayoutBinding b[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        b[i].binding = i;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 4;
    lci.pBindings = b;
    if (vkCreateDescriptorSetLayout(device, &lci, nullptr, &set_layout_) != VK_SUCCESS)
        return false;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(uint32_t) * 2;

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &set_layout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &pipeline_layout_) !=
        VK_SUCCESS)
        return false;

    std::vector<unsigned int> code;
    if (!gfx::load_shader_source("shaders/skin_palette.comp.spv", code)) {
        LOG_ERROR("[Skin] missing shaders/skin_palette.comp.spv");
        return false;
    }
    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = code.size() * sizeof(unsigned int);
    smci.pCode = code.data();
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &smci, nullptr, &mod) != VK_SUCCESS)
        return false;

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = mod;
    stage.pName = "main";
    VkComputePipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage = stage;
    ci.layout = pipeline_layout_;
    const VkResult pr =
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr,
                                 &palette_pipeline_);
    vkDestroyShaderModule(device, mod, nullptr);
    if (pr != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxFrames * 4};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = kMaxFrames;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device, &pci, nullptr, &pool_) != VK_SUCCESS)
        return false;

    std::array<VkDescriptorSetLayout, kMaxFrames> layouts{};
    layouts.fill(set_layout_);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = kMaxFrames;
    ai.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &ai, sets_.data()) != VK_SUCCESS) {
        destroy_compute(device, VK_NULL_HANDLE);
        return false;
    }

    // Worlds rebound after GpuCulling::build_scene. Bind IBM/meta/palette now.
    for (uint32_t f = 0; f < kMaxFrames; ++f) {
        VkDescriptorBufferInfo infos[3] = {
            {ibm_buffer_.buffer, 0, ibm_buffer_.info.size},
            {meta_buffer_.buffer, 0, meta_buffer_.info.size},
            {joint_buffers_[f].buffer, 0, joint_buffers_[f].info.size},
        };
        VkWriteDescriptorSet writes[3]{};
        for (uint32_t i = 0; i < 3; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = sets_[f];
            writes[i].dstBinding = i + 1;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
    }
    return true;
}

void SkinSystem::write_set(uint32_t frame, const gfx::AllocatedBuffer& worlds) {
    if (frame >= kMaxFrames || sets_[frame] == VK_NULL_HANDLE ||
        worlds.buffer == VK_NULL_HANDLE)
        return;
    VkDescriptorBufferInfo info{worlds.buffer, 0, worlds.info.size};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = sets_[frame];
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &info;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
}

void SkinSystem::bind_worlds(const gfx::AllocatedBuffer& worlds0,
                             const gfx::AllocatedBuffer& worlds1) {
    write_set(0, worlds0);
    write_set(1, worlds1);
}

void SkinSystem::record(VkCommandBuffer cmd, uint32_t frame_index,
                        const gfx::AllocatedBuffer& worlds, uint32_t world_count) {
    if (!gpu_compute_ready() || frame_index >= kMaxFrames ||
        worlds.buffer == VK_NULL_HANDLE)
        return;

    VkMemoryBarrier host{};
    host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    host.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &host, 0,
                         nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, palette_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_,
                            0, 1, &sets_[frame_index], 0, nullptr);
    const uint32_t pc[2] = {total_joints_, world_count};
    vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(pc), pc);
    vkCmdDispatch(cmd, (total_joints_ + 63u) / 64u, 1, 1);

    VkMemoryBarrier to_vs{};
    to_vs.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    to_vs.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    to_vs.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &to_vs, 0,
                         nullptr, 0, nullptr);
}

} // namespace scene
