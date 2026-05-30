#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "scene/GltfLoader.h"
#include "gfx/MeshData.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <map>
#include <tiny_gltf.h>

bool scene::GltfLoader::load_model(const std::string &filename,
                                  tinygltf::Model &model) {
    tinygltf::TinyGLTF loader{};
    std::string err;
    std::string warn;

    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
    if (!ret) {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
    }

    if (!warn.empty()) {
        std::cerr << "[GLTF] Warning while loading '" << filename << "': " << warn << std::endl;
    }
    if (!err.empty()) {
        std::cerr << "[GLTF] Error while loading '" << filename << "': " << err << std::endl;
    }

    return ret;
}

std::vector<gfx::MaterialID>
scene::GltfLoader::extract_material_data(const std::string &filename,
                                        const tinygltf::Model &model,
                                        gfx::Renderer &renderer) {

    std::vector<gfx::MaterialID> material_lookup{};
    material_lookup.reserve(model.materials.size());
    auto texture_path = std::filesystem::path(filename).parent_path();

    auto get_safe_image_path = [&](int texture_index) -> std::pair<std::string, std::filesystem::path> {
        if (texture_index < 0 || texture_index >= (int)model.textures.size())
            return {"", {}};
        const auto& tex = model.textures[texture_index];
        if (tex.source < 0 || tex.source >= (int)model.images.size())
            return {"", {}};
        const auto& img = model.images[tex.source];
        std::filesystem::path uri_path;
        if (!img.uri.empty()) {
            uri_path = texture_path / img.uri;
        } else if (img.bufferView >= 0) {
            // Embedded image via bufferView (common in .glb). Not supported in current path.
            // Future: decode via tinygltf or custom loader and pass raw pixels to TextureManager.
            std::cerr << "[GLTF] Warning: Image uses bufferView (embedded) - currently unsupported, skipping texture.\n";
        }
        return {img.name, uri_path};
    };

    // create materials in the material manager using the proper typed glTF structures
    for (const auto &mat : model.materials) {
        gfx::Material material{};

        // --- PBR base values (with correct glTF 2.0 defaults) ---
        const auto& pbr = mat.pbrMetallicRoughness;

        if (pbr.baseColorFactor.size() == 4) {
            material.albedo = glm::vec4(
                static_cast<float>(pbr.baseColorFactor[0]),
                static_cast<float>(pbr.baseColorFactor[1]),
                static_cast<float>(pbr.baseColorFactor[2]),
                static_cast<float>(pbr.baseColorFactor[3]));
        } else {
            material.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        material.metallic  = static_cast<float>(pbr.metallicFactor);
        material.roughness = static_cast<float>(pbr.roughnessFactor);
        material.normalStrength = 1.0f; // reasonable default; can be driven by normalTexture.scale later

        // --- Textures via proper glTF accessors (not the legacy ParameterMap) ---
        // baseColor
        {
            auto [img_name, uri] = get_safe_image_path(pbr.baseColorTexture.index);
            if (!uri.empty()) {
                material.albedo_texture_index =
                    renderer.texture_manager.get_texture_handle(img_name, uri);
            }
        }

        // metallicRoughness (typical layout: G=roughness, B=metallic)
        {
            auto [img_name, uri] = get_safe_image_path(pbr.metallicRoughnessTexture.index);
            if (!uri.empty()) {
                material.roughness_texture_index =
                    renderer.texture_manager.get_texture_handle(img_name, uri);
            }
        }

        // normalTexture (top-level on Material)
        {
            auto [img_name, uri] = get_safe_image_path(mat.normalTexture.index);
            if (!uri.empty()) {
                material.normal_texture_index =
                    renderer.texture_manager.get_texture_handle(img_name, uri);
                // If we want to honor scale: material.normalStrength = static_cast<float>(mat.normalTexture.scale);
            }
        }

        // emissiveTexture
        {
            auto [img_name, uri] = get_safe_image_path(mat.emissiveTexture.index);
            if (!uri.empty()) {
                material.emissive_texture_index =
                    renderer.texture_manager.get_texture_handle(img_name, uri);
            }
        }

        // occlusionTexture (AO)
        {
            auto [img_name, uri] = get_safe_image_path(mat.occlusionTexture.index);
            if (!uri.empty()) {
                material.ao_texture_index =
                    renderer.texture_manager.get_texture_handle(img_name, uri);
            }
        }

        // Emissive factor (simple average into the existing scalar for now)
        if (mat.emissiveFactor.size() == 3) {
            float avg = (static_cast<float>(mat.emissiveFactor[0]) +
                         static_cast<float>(mat.emissiveFactor[1]) +
                         static_cast<float>(mat.emissiveFactor[2])) / 3.0f;
            material.emissive = avg;
        }

        gfx::MaterialID id = renderer.material_manager.create_material(material);
        material_lookup.push_back(id);
    }

    return material_lookup;
}

std::vector<gfx::MeshPrimitiveID>
scene::GltfLoader::extract_mesh_data(const tinygltf::Model &model,
                                    gfx::Renderer &renderer) {

    std::vector<gfx::MeshPrimitiveID> meshes{};
    meshes.reserve(model.meshes.size() *
                   2); // most glTF meshes have multiple primitives

    for (const auto &mesh : model.meshes) {
        for (const auto &primitive : mesh.primitives) {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES)
                continue;

            // Early outs
            auto pos_it = primitive.attributes.find("POSITION");
            auto norm_it = primitive.attributes.find("NORMAL");
            auto tex_it = primitive.attributes.find("TEXCOORD_0");

            if (pos_it == primitive.attributes.end() ||
                norm_it == primitive.attributes.end() ||
                tex_it == primitive.attributes.end())
                continue;

            const auto &pos_acc = model.accessors[pos_it->second];
            const auto &norm_acc = model.accessors[norm_it->second];
            const auto &tex_acc = model.accessors[tex_it->second];

            if (pos_acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
                pos_acc.type != TINYGLTF_TYPE_VEC3)
                continue;

            const size_t num_vertices = pos_acc.count;

            // === Hoisted pointers & strides (critical for Quest 3 load perf)
            // ===
            auto get_accessor_data =
                [&](const tinygltf::Accessor &acc) -> const uint8_t * {
                if (acc.bufferView < 0)
                    return nullptr;
                const auto &bv = model.bufferViews[acc.bufferView];
                const auto &buf = model.buffers[bv.buffer];
                return &buf.data[bv.byteOffset + acc.byteOffset];
            };

            auto tang_it = primitive.attributes.find("TANGENT");
            const tinygltf::Accessor *tang_acc = nullptr;
            const uint8_t *tang_ptr = nullptr;
            int tang_stride = 0;

            if (tang_it != primitive.attributes.end()) {
                tang_acc = &model.accessors[tang_it->second];
                tang_ptr = get_accessor_data(*tang_acc);
                tang_stride = tang_acc->ByteStride(
                    model.bufferViews[tang_acc->bufferView]);
                if (tang_stride == 0)
                    tang_stride = 16; // vec4 float
            }

            const uint8_t *pos_ptr = get_accessor_data(pos_acc);
            const uint8_t *norm_ptr = get_accessor_data(norm_acc);
            const uint8_t *tex_ptr = get_accessor_data(tex_acc);

            int pos_stride =
                pos_acc.ByteStride(model.bufferViews[pos_acc.bufferView]);
            int norm_stride =
                norm_acc.ByteStride(model.bufferViews[norm_acc.bufferView]);
            int tex_stride =
                tex_acc.ByteStride(model.bufferViews[tex_acc.bufferView]);

            if (pos_stride == 0)
                pos_stride = 12; // vec3 float
            if (norm_stride == 0)
                norm_stride = 12;
            if (tex_stride == 0)
                tex_stride = 8; // vec2 float

            // === AABB (single pass) ===
            core::AABB aabb{{FLT_MAX, FLT_MAX, FLT_MAX},
                            {-FLT_MAX, -FLT_MAX, -FLT_MAX}};
            for (size_t i = 0; i < num_vertices; ++i) {
                const float *p =
                    reinterpret_cast<const float *>(pos_ptr + i * pos_stride);
                aabb.min.x = std::min(aabb.min.x, p[0]);
                aabb.min.y = std::min(aabb.min.y, p[1]);
                aabb.min.z = std::min(aabb.min.z, p[2]);
                aabb.max.x = std::max(aabb.max.x, p[0]);
                aabb.max.y = std::max(aabb.max.y, p[1]);
                aabb.max.z = std::max(aabb.max.z, p[2]);
            }

            // === Vertex packing ===
            gfx::MeshData mesh_data;
            mesh_data.vertices.resize(num_vertices);

            for (size_t i = 0; i < num_vertices; ++i) {
                gfx::Vertex &v = mesh_data.vertices[i];

                // Position
                {
                    const float *p = reinterpret_cast<const float *>(
                        pos_ptr + i * pos_stride);
                    v.position[0] = p[0];
                    v.position[1] = p[1];
                    v.position[2] = p[2];
                }

                // Normal
                {
                    const float *n = reinterpret_cast<const float *>(
                        norm_ptr + i * norm_stride);
                    v.normal[0] = n[0];
                    v.normal[1] = n[1];
                    v.normal[2] = n[2];
                }

                // Tangent (from glTF or default)
                if (tang_ptr && tang_acc) {
                    const float *t = reinterpret_cast<const float *>(
                        tang_ptr + i * tang_stride);
                    v.tangent[0] = t[0];
                    v.tangent[1] = t[1];
                    v.tangent[2] = t[2];
                    v.tangent[3] = t[3]; // handedness
                } else {
                    // Default tangent (valid for many cases)
                    v.tangent[0] = 1.0f;
                    v.tangent[1] = 0.0f;
                    v.tangent[2] = 0.0f;
                    v.tangent[3] = 1.0f;
                }

                // UV → packed uint16_t (little-endian) into 4x uint8_t
                {
                    const float *t = reinterpret_cast<const float *>(
                        tex_ptr + i * tex_stride);
                    uint16_t u = static_cast<uint16_t>(
                        std::clamp(t[0], 0.0f, 1.0f) * 65535.0f);
                    uint16_t vval = static_cast<uint16_t>(
                        std::clamp(t[1], 0.0f, 1.0f) * 65535.0f);

                    v.uv[0] = static_cast<uint8_t>(u & 0xFF);
                    v.uv[1] = static_cast<uint8_t>((u >> 8) & 0xFF);
                    v.uv[2] = static_cast<uint8_t>(vval & 0xFF);
                    v.uv[3] = static_cast<uint8_t>((vval >> 8) & 0xFF);
                }

                // UV1 = zero for now
                v.uv[4] = v.uv[0];
                v.uv[5] = v.uv[1];
                v.uv[6] = v.uv[2];
                v.uv[7] = v.uv[3];
                // Skinning defaults
                v.blend_weights[0] = 255;
                v.blend_weights[1] = v.blend_weights[2] = v.blend_weights[3] =
                    0;
                v.blend_indices[0] = v.blend_indices[1] = v.blend_indices[2] =
                    v.blend_indices[3] = 0;
            }

            // === ADD THIS: Index buffer support (mandatory for real assets)
            // ===
            if (primitive.indices >= 0) {
                const auto &idx_acc = model.accessors[primitive.indices];
                if (idx_acc.componentType ==
                        TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ||
                    idx_acc.componentType ==
                        TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {

                    const uint8_t *idx_ptr = get_accessor_data(idx_acc);
                    int idx_stride = idx_acc.ByteStride(
                        model.bufferViews[idx_acc.bufferView]);
                    if (idx_stride == 0) {
                        idx_stride = (idx_acc.componentType ==
                                      TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                                         ? 2
                                         : 4;
                    }

                    mesh_data.indices.resize(idx_acc.count);
                    if (idx_acc.componentType ==
                        TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        for (size_t i = 0; i < idx_acc.count; ++i) {
                            mesh_data.indices[i] =
                                reinterpret_cast<const uint16_t *>(
                                    idx_ptr + i * idx_stride)[0];
                        }
                    } else {
                        for (size_t i = 0; i < idx_acc.count; ++i) {
                            mesh_data.indices[i] =
                                reinterpret_cast<const uint32_t *>(
                                    idx_ptr + i * idx_stride)[0];
                        }
                    }
                }
            }

            mesh_data.local_aabb = aabb;
            // mesh_data.material = material_lookup[primitive.material]; // TODO

            meshes.push_back(renderer.mesh_manager.add_mesh(mesh_data));
        }
    }
    return meshes;
}

std::vector<double>
scene::GltfLoader::value_or_ident(const std::vector<double> &value,
                                 const size_t len) {
    if (value.size() < len) {
        std::vector<double> result{};
        result.reserve(len);
        for (size_t i = 0; i < len; i++)
            result.push_back(1.0);
        return result;
    } else {
        return value;
    }
}

glm::mat4 scene::GltfLoader::extract_node_transform(const tinygltf::Node &node) {
    // glTF nodes may specify either a 4x4 matrix or separate TRS.
    // Matrix takes precedence when present and has 16 elements.
    if (node.matrix.size() == 16) {
        // tinygltf stores column-major (matches glm default)
        return glm::make_mat4(node.matrix.data());
    }

    auto translation = value_or_ident(node.translation, 3U);
    auto rotation = value_or_ident(node.rotation, 4U);
    auto scale = value_or_ident(node.scale, 3U);
    return glm::translate(
               glm::mat4(1.0f),
               glm::vec3(translation[0], translation[1], translation[2])) *
           glm::mat4_cast(
               glm::quat(rotation[0], rotation[1], rotation[2], rotation[3])) *
           glm::scale(glm::mat4(1.0f), glm::vec3(scale[0], scale[1], scale[2]));
}
