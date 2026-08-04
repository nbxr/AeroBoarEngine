#include "scene/Morph.h"
#include "core/Log.h"
#include "gfx/Vertex.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <tiny_gltf.h>

namespace scene {
namespace {

bool read_vec3_accessor(const tinygltf::Model& model, int accessor_index,
                        std::vector<glm::vec3>& out) {
    out.clear();
    if (accessor_index < 0 ||
        accessor_index >= static_cast<int>(model.accessors.size()))
        return false;
    const auto& acc = model.accessors[static_cast<size_t>(accessor_index)];
    if (acc.type != TINYGLTF_TYPE_VEC3 ||
        acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
        return false;
    if (acc.bufferView < 0 ||
        acc.bufferView >= static_cast<int>(model.bufferViews.size()))
        return false;
    const auto& bv = model.bufferViews[static_cast<size_t>(acc.bufferView)];
    if (bv.buffer < 0 || bv.buffer >= static_cast<int>(model.buffers.size()))
        return false;
    const auto& buf = model.buffers[static_cast<size_t>(bv.buffer)];
    int stride = acc.ByteStride(bv);
    if (stride <= 0)
        stride = 12;
    const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
    out.resize(acc.count);
    for (size_t i = 0; i < acc.count; ++i) {
        const float* f =
            reinterpret_cast<const float*>(base + i * static_cast<size_t>(stride));
        out[i] = glm::vec3(f[0], f[1], f[2]);
    }
    return true;
}

} // namespace

void MorphSystem::clear() {
    instances_.clear();
    gltf_node_to_morph_.clear();
}

uint32_t MorphSystem::load_from_gltf(const tinygltf::Model& model,
                                     const std::vector<uint32_t>& mesh_lookup) {
    clear();
    size_t prim_flat = 0;
    uint32_t loaded = 0;

    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        const auto& mesh = model.meshes[mi];
        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi, ++prim_flat) {
            const auto& prim = mesh.primitives[pi];
            if (prim.targets.empty())
                continue;
            if (prim_flat >= mesh_lookup.size())
                continue;

            const auto pos_it = prim.attributes.find("POSITION");
            if (pos_it == prim.attributes.end())
                continue;

            MorphInstance inst{};
            inst.mesh_primitive_index = mesh_lookup[prim_flat];
            if (!read_vec3_accessor(model, pos_it->second, inst.base_positions))
                continue;
            inst.vertex_count = static_cast<uint32_t>(inst.base_positions.size());
            if (inst.vertex_count == 0)
                continue;

            const auto nrm_it = prim.attributes.find("NORMAL");
            if (nrm_it != prim.attributes.end())
                read_vec3_accessor(model, nrm_it->second, inst.base_normals);
            if (inst.base_normals.size() != inst.vertex_count)
                inst.base_normals.clear();

            inst.target_count = static_cast<uint32_t>(prim.targets.size());
            inst.target_positions.assign(
                static_cast<size_t>(inst.target_count) * inst.vertex_count,
                glm::vec3(0.0f));
            const bool has_base_n = !inst.base_normals.empty();
            if (has_base_n) {
                inst.target_normals.assign(
                    static_cast<size_t>(inst.target_count) * inst.vertex_count,
                    glm::vec3(0.0f));
            }

            bool any_target = false;
            for (uint32_t t = 0; t < inst.target_count; ++t) {
                const auto& tgt = prim.targets[t];
                auto tp = tgt.find("POSITION");
                if (tp == tgt.end())
                    continue;
                std::vector<glm::vec3> deltas;
                if (!read_vec3_accessor(model, tp->second, deltas) ||
                    deltas.size() != inst.vertex_count)
                    continue;
                any_target = true;
                const size_t base =
                    static_cast<size_t>(t) * inst.vertex_count;
                for (uint32_t v = 0; v < inst.vertex_count; ++v)
                    inst.target_positions[base + v] = deltas[v];

                if (has_base_n) {
                    auto tn = tgt.find("NORMAL");
                    if (tn != tgt.end()) {
                        std::vector<glm::vec3> nd;
                        if (read_vec3_accessor(model, tn->second, nd) &&
                            nd.size() == inst.vertex_count) {
                            for (uint32_t v = 0; v < inst.vertex_count; ++v)
                                inst.target_normals[base + v] = nd[v];
                        }
                    }
                }
            }
            if (!any_target)
                continue;

            // Default weights: mesh.weights, else zeros.
            inst.weights.assign(inst.target_count, 0.0f);
            if (mesh.weights.size() >= inst.target_count) {
                for (uint32_t t = 0; t < inst.target_count; ++t)
                    inst.weights[t] = static_cast<float>(mesh.weights[t]);
            }
            inst.dirty = true;

            instances_.push_back(std::move(inst));
            ++loaded;
        }
    }

    LOG_INFO("[Morph] Loaded " << loaded << " morph primitive(s)");
    return loaded;
}

void MorphSystem::bind_nodes(const tinygltf::Model& model) {
    gltf_node_to_morph_.assign(model.nodes.size(), kInvalidMorph);

    // Map mesh index → first morph instance that uses a primitive of that mesh.
    // AnimatedMorphCube: one mesh, one prim, one instance.
    std::vector<uint32_t> mesh_to_morph(model.meshes.size(), kInvalidMorph);
    // Rebuild by re-walking (instances only store mesh_primitive_index).
    // Instead: store gltf mesh index during load. For simplicity, match by
    // iterating nodes: each node.mesh may own morphs on that mesh's prims.
    // We assign the first morph instance whose primitive belongs to this mesh.
    // Load order: instances are in mesh×prim order with targets only.
    size_t inst_i = 0;
    size_t prim_flat = 0;
    std::vector<uint32_t> prim_to_morph;
    // Rebuild prim_flat → morph from load order: only morphing prims were stored.
    // Re-scan model for targets to align.
    for (size_t mi = 0; mi < model.meshes.size(); ++mi) {
        const auto& mesh = model.meshes[mi];
        uint32_t first_for_mesh = kInvalidMorph;
        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            if (mesh.primitives[pi].targets.empty())
                continue;
            if (inst_i >= instances_.size())
                break;
            if (first_for_mesh == kInvalidMorph)
                first_for_mesh = static_cast<uint32_t>(inst_i);
            instances_[inst_i].gltf_node_index = ~0u; // filled below
            ++inst_i;
        }
        mesh_to_morph[mi] = first_for_mesh;
    }

    for (size_t ni = 0; ni < model.nodes.size(); ++ni) {
        const auto& node = model.nodes[ni];
        if (node.mesh < 0 ||
            static_cast<size_t>(node.mesh) >= mesh_to_morph.size())
            continue;
        const uint32_t morph = mesh_to_morph[static_cast<size_t>(node.mesh)];
        if (morph == kInvalidMorph)
            continue;
        gltf_node_to_morph_[ni] = morph;
        instances_[morph].gltf_node_index = static_cast<uint32_t>(ni);

        // Node weights override mesh defaults when present.
        if (!node.weights.empty() &&
            node.weights.size() >= instances_[morph].weights.size()) {
            for (size_t t = 0; t < instances_[morph].weights.size(); ++t)
                instances_[morph].weights[t] =
                    static_cast<float>(node.weights[t]);
            instances_[morph].dirty = true;
        }
    }
}

uint32_t MorphSystem::morph_for_node(uint32_t gltf_node) const {
    if (gltf_node >= gltf_node_to_morph_.size())
        return kInvalidMorph;
    return gltf_node_to_morph_[gltf_node];
}

void MorphSystem::set_weights(uint32_t morph_index, const float* weights,
                              uint32_t count) {
    if (morph_index >= instances_.size() || !weights || count == 0)
        return;
    MorphInstance& inst = instances_[morph_index];
    const uint32_t n = std::min(count, inst.target_count);
    bool changed = false;
    for (uint32_t i = 0; i < n; ++i) {
        if (std::fabs(inst.weights[i] - weights[i]) > 1e-7f) {
            changed = true;
            break;
        }
    }
    if (!changed && n == inst.target_count)
        return;
    for (uint32_t i = 0; i < n; ++i)
        inst.weights[i] = weights[i];
    inst.dirty = true;
}

const std::vector<float>& MorphSystem::weights(uint32_t morph_index) const {
    static const std::vector<float> kEmpty;
    if (morph_index >= instances_.size())
        return kEmpty;
    return instances_[morph_index].weights;
}

void MorphSystem::apply(gfx::MeshManager& meshes) {
    for (MorphInstance& inst : instances_) {
        if (!inst.dirty)
            continue;
        if (inst.vertex_count == 0 || inst.mesh_primitive_index == ~0u) {
            inst.dirty = false;
            continue;
        }

        std::vector<gfx::Vertex> verts(inst.vertex_count);
        // Seed from mesh cache (UVs, tangents, skin attrs) then overwrite P/N.
        if (!meshes.copy_primitive_vertices(inst.mesh_primitive_index, verts.data(),
                                            inst.vertex_count)) {
            LOG_ERROR("[Morph] copy_primitive_vertices failed for mesh "
                      << inst.mesh_primitive_index);
            inst.dirty = false;
            continue;
        }

        const bool has_n =
            !inst.base_normals.empty() &&
            inst.base_normals.size() == inst.vertex_count;

        for (uint32_t v = 0; v < inst.vertex_count; ++v) {
            glm::vec3 p = inst.base_positions[v];
            glm::vec3 n =
                has_n ? inst.base_normals[v] : glm::vec3(0.0f, 0.0f, 1.0f);
            for (uint32_t t = 0; t < inst.target_count; ++t) {
                const float w = inst.weights[t];
                if (std::fabs(w) < 1e-8f)
                    continue;
                const size_t idx =
                    static_cast<size_t>(t) * inst.vertex_count + v;
                p += w * inst.target_positions[idx];
                if (has_n && !inst.target_normals.empty())
                    n += w * inst.target_normals[idx];
            }
            verts[v].position[0] = p.x;
            verts[v].position[1] = p.y;
            verts[v].position[2] = p.z;
            if (has_n) {
                const float len = glm::length(n);
                if (len > 1e-8f)
                    n /= len;
                verts[v].normal[0] = n.x;
                verts[v].normal[1] = n.y;
                verts[v].normal[2] = n.z;
            }
        }

        if (!meshes.write_primitive_vertices(inst.mesh_primitive_index,
                                             verts.data(), inst.vertex_count)) {
            LOG_ERROR("[Morph] write_primitive_vertices failed for mesh "
                      << inst.mesh_primitive_index);
        }
        inst.dirty = false;
    }
}

} // namespace scene
