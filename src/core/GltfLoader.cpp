#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "GltfLoader.h"
#include "MeshData.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tiny_gltf.h>
#include <map>

bool core::GltfLoader::load_model(const std::string &filename,
                                  tinygltf::Model &model) {
    tinygltf::TinyGLTF loader{};
    std::string err;
    std::string warn;
    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
    if (!ret) {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
    }

    return ret;
}

std::vector<core::MeshData> core::GltfLoader::extract_mesh_data(const tinygltf::Model &model) {
    std::vector<core::MeshData> meshes;
    meshes.reserve(model.meshes.size());

    for (const auto &mesh : model.meshes) {
        for (const auto &primitive : mesh.primitives) {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
                continue;
            }

            core::MeshData mesh_data;

            auto get_accessor_data = [&](const tinygltf::Accessor &acc) -> const uint8_t* {
                if (acc.bufferView < 0) return nullptr;
                const auto &bv = model.bufferViews[acc.bufferView];
                const auto &buf = model.buffers[bv.buffer];
                return &buf.data.data()[bv.byteOffset + acc.byteOffset];
            };

            // Positions (required)
            auto pos_it = primitive.attributes.find("POSITION");
            if (pos_it == primitive.attributes.end()) continue;
            const auto &pos_acc = model.accessors[pos_it->second];
            if (pos_acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || pos_acc.type != TINYGLTF_TYPE_VEC3) continue;

            const size_t num_vertices = pos_acc.count;
            const uint8_t* pos_data = get_accessor_data(pos_acc);
            if (!pos_data) continue;

            mesh_data.positions.resize(num_vertices * 3);
            const auto& pos_bv = model.bufferViews[pos_acc.bufferView];
            int pos_stride = pos_acc.ByteStride(pos_bv);
            if (pos_stride == 0) pos_stride = 12;
            for (size_t i = 0; i < num_vertices; ++i) {
                memcpy(&mesh_data.positions[i*3], pos_data + i * pos_stride, 12);
            }

            // Normals (optional)
            auto norm_it = primitive.attributes.find("NORMAL");
            if (norm_it != primitive.attributes.end()) {
                const auto &n_acc = model.accessors[norm_it->second];
                if (n_acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && n_acc.type == TINYGLTF_TYPE_VEC3) {
                    const uint8_t* n_data = get_accessor_data(n_acc);
                    if (n_data) {
                        mesh_data.normals.resize(num_vertices * 3);
                        const auto& n_bv = model.bufferViews[n_acc.bufferView];
                        int n_stride = n_acc.ByteStride(n_bv);
                        if (n_stride == 0) n_stride = 12;
                        for (size_t i = 0; i < num_vertices; ++i) {
                            memcpy(&mesh_data.normals[i*3], n_data + i * n_stride, 12);
                        }
                    } else {
                        mesh_data.normals.assign(num_vertices * 3, 0.0f);
                    }
                } else {
                    mesh_data.normals.assign(num_vertices * 3, 0.0f);
                }
            } else {
                mesh_data.normals.assign(num_vertices * 3, 0.0f);
            }

            // Texcoords (optional)
            auto tex_it = primitive.attributes.find("TEXCOORD_0");
            if (tex_it != primitive.attributes.end()) {
                const auto &t_acc = model.accessors[tex_it->second];
                if (t_acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && t_acc.type == TINYGLTF_TYPE_VEC2) {
                    const uint8_t* t_data = get_accessor_data(t_acc);
                    if (t_data) {
                        mesh_data.texcoords.resize(num_vertices * 2);
                        const auto& t_bv = model.bufferViews[t_acc.bufferView];
                        int t_stride = t_acc.ByteStride(t_bv);
                        if (t_stride == 0) t_stride = 8;
                        for (size_t i = 0; i < num_vertices; ++i) {
                            memcpy(&mesh_data.texcoords[i*2], t_data + i * t_stride, 8);
                        }
                    }
                }
            }

            // Indices (your original code, unchanged)
            if (primitive.indices != -1) {
                const auto &idx_acc = model.accessors[primitive.indices];
                const uint8_t *idx_data = get_accessor_data(idx_acc);
                if (idx_data) {
                    if (idx_acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        mesh_data.indices.resize(idx_acc.count);
                        const uint8_t *src = idx_data;
                        for(size_t i=0; i<idx_acc.count; ++i) {
                            mesh_data.indices[i] = static_cast<uint32_t>(src[i]);
                        }
                    } else if (idx_acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        mesh_data.indices.resize(idx_acc.count);
                        const uint16_t *src = reinterpret_cast<const uint16_t *>(idx_data);
                        for(size_t i=0; i<idx_acc.count; ++i) {
                            mesh_data.indices[i] = static_cast<uint32_t>(src[i]);
                        }
                    } else if (idx_acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        mesh_data.indices.resize(idx_acc.count);
                        memcpy(mesh_data.indices.data(), idx_data, idx_acc.count * sizeof(uint32_t));
                    }
                }
            }

            // AABB
            core::AABB aabb;
            if (num_vertices > 0) {
                aabb.min = glm::make_vec3(&mesh_data.positions[0]);
                aabb.max = glm::make_vec3(&mesh_data.positions[0]);
                for (size_t i = 1; i < num_vertices; ++i) {
                    glm::vec3 pos = glm::make_vec3(&mesh_data.positions[i * 3]);
                    aabb.min = glm::min(aabb.min, pos);
                    aabb.max = glm::max(aabb.max, pos);
                }
            }
            mesh_data.local_aabb = aabb;

            meshes.push_back(std::move(mesh_data));
        }
    }
    return meshes;
}