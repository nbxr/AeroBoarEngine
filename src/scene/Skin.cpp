#include "scene/Skin.h"
#include "gfx/BufferUtils.h"
#include "core/Log.h"

#include <algorithm>
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
    for (uint32_t f = 0; f < kMaxFrames; ++f)
        gfx::BufferUtils::destroy_buffer(device, allocator, joint_buffers_[f]);
    gpu_ready_ = false;
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
    for (uint32_t f = 0; f < kMaxFrames; ++f)
        gfx::BufferUtils::destroy_buffer(device, allocator, joint_buffers_[f]);

    const VkDeviceSize bytes = joint_buffer_size();
    for (uint32_t f = 0; f < kMaxFrames; ++f) {
        if (!gfx::BufferUtils::initialize_buffer(device, allocator, bytes,
                                                 joint_buffers_[f])) {
            LOG_ERROR("[Skin] Failed to create joint matrix buffer");
            gpu_ready_ = false;
            return false;
        }
        // Identity fill
        auto* dst = static_cast<glm::mat4*>(joint_buffers_[f].mapped_data);
        for (uint32_t i = 0; i < std::max(1u, total_joints_); ++i)
            dst[i] = glm::mat4(1.0f);
    }
    gpu_ready_ = true;
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

} // namespace scene
