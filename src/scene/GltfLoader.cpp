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

// Helper to read a TEXCOORD (always VEC2) from an accessor, properly handling
// normalized integer formats that some optimized glTFs use.
static glm::vec2 GetTexcoordFromAccessor(const tinygltf::Model& model,
                                         const tinygltf::Accessor& acc,
                                         size_t index) {
    if (acc.type != TINYGLTF_TYPE_VEC2 || acc.bufferView < 0) {
        return glm::vec2(0.0f, 0.0f);
    }

    const auto& bv = model.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)model.buffers.size()) {
        return glm::vec2(0.0f, 0.0f);
    }

    const auto& buf = model.buffers[bv.buffer];
    const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;

    int stride = acc.ByteStride(bv);
    if (stride == 0) {
        stride = tinygltf::GetNumComponentsInType(acc.type) *
                 tinygltf::GetComponentSizeInBytes(acc.componentType);
    }

    const uint8_t* ptr = base + index * stride;

    switch (acc.componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            const float* f = reinterpret_cast<const float*>(ptr);
            return {f[0], f[1]};
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
            const uint16_t* s = reinterpret_cast<const uint16_t*>(ptr);
            if (acc.normalized) {
                return {float(s[0]) / 65535.0f, float(s[1]) / 65535.0f};
            } else {
                // Uncommon for UVs, but handle it
                return {float(s[0]), float(s[1])};
            }
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
            const uint8_t* b = reinterpret_cast<const uint8_t*>(ptr);
            if (acc.normalized) {
                return {float(b[0]) / 255.0f, float(b[1]) / 255.0f};
            } else {
                return {float(b[0]), float(b[1])};
            }
        }
        // SHORT and BYTE are theoretically possible if normalized, but rare for UVs.
        // Adding them for completeness:
        case TINYGLTF_COMPONENT_TYPE_SHORT: {
            const int16_t* s = reinterpret_cast<const int16_t*>(ptr);
            if (acc.normalized) {
                return {std::max(-1.0f, float(s[0]) / 32767.0f),
                        std::max(-1.0f, float(s[1]) / 32767.0f)};
            }
            return {float(s[0]), float(s[1])};
        }
        case TINYGLTF_COMPONENT_TYPE_BYTE: {
            const int8_t* b = reinterpret_cast<const int8_t*>(ptr);
            if (acc.normalized) {
                return {std::max(-1.0f, float(b[0]) / 127.0f),
                        std::max(-1.0f, float(b[1]) / 127.0f)};
            }
            return {float(b[0]), float(b[1])};
        }
        default:
            return glm::vec2(0.0f, 0.0f);
    }
}

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
        material.albedo_texture_index = gfx::Material::NO_TEXTURE;
        material.normal_texture_index = gfx::Material::NO_TEXTURE;
        material.roughness_texture_index = gfx::Material::NO_TEXTURE;
        material.emissive_texture_index = gfx::Material::NO_TEXTURE;
        material.ao_texture_index = gfx::Material::NO_TEXTURE;
        material.sampler_index = gfx::Material::NO_TEXTURE;
        material.flags = 0;

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
                    renderer.texture_manager.get_texture_handle(img_name, uri.string(), /*is_srgb=*/true);
            }
        }

        // metallicRoughness (typical layout: G=roughness, B=metallic)
        {
            auto [img_name, uri] = get_safe_image_path(pbr.metallicRoughnessTexture.index);
            if (!uri.empty()) {
                material.roughness_texture_index =
                    renderer.texture_manager.get_texture_handle(img_name, uri.string());
            }
        }

        // normalTexture (top-level on Material)
        {
            auto [img_name, uri] = get_safe_image_path(mat.normalTexture.index);
            if (!uri.empty()) {
                material.normal_texture_index =
                    renderer.texture_manager.get_texture_handle(img_name, uri.string());
                // If we want to honor scale: material.normalStrength = static_cast<float>(mat.normalTexture.scale);
            }
        }

        // emissiveTexture
        {
            auto [img_name, uri] = get_safe_image_path(mat.emissiveTexture.index);
            if (!uri.empty()) {
                material.emissive_texture_index =
                    renderer.texture_manager.get_texture_handle(img_name, uri.string());
            }
        }

        // occlusionTexture (AO)
        {
            auto [img_name, uri] = get_safe_image_path(mat.occlusionTexture.index);
            if (!uri.empty()) {
                material.ao_texture_index =
                    renderer.texture_manager.get_texture_handle(img_name, uri.string());
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

std::vector<gfx::Light>
scene::GltfLoader::extract_light_data(const tinygltf::Model &model) {
    std::vector<gfx::Light> lights;
    lights.reserve(model.lights.size());

    for (size_t i = 0; i < model.lights.size(); ++i) {
        const auto& tinyLight = model.lights[i];
        gfx::Light l{};
        l.intensity = static_cast<float>(tinyLight.intensity);

        if (tinyLight.color.size() >= 3) {
            l.color = glm::vec3(
                static_cast<float>(tinyLight.color[0]),
                static_cast<float>(tinyLight.color[1]),
                static_cast<float>(tinyLight.color[2]));
        } else {
            l.color = glm::vec3(1.0f);
        }

        const std::string& t = tinyLight.type;
        if (t == "directional") {
            l.type = gfx::LightType::Directional;
            // Default to-light; overwritten by node world transform when attached.
            l.direction = glm::normalize(glm::vec3(0.35f, 1.0f, 0.25f));
        } else if (t == "point") {
            l.type = gfx::LightType::Point;
            l.range = static_cast<float>(tinyLight.range);
        } else if (t == "spot") {
            l.type = gfx::LightType::Spot;
            l.range = static_cast<float>(tinyLight.range);
            l.innerConeAngle = static_cast<float>(tinyLight.spot.innerConeAngle);
            l.outerConeAngle = static_cast<float>(tinyLight.spot.outerConeAngle);
            // Emission −Z until a node transform is applied.
            l.direction = glm::vec3(0.0f, 0.0f, -1.0f);
        }

        lights.push_back(l);
    }

    return lights;
}

void scene::GltfLoader::apply_world_transform_to_light(gfx::Light& light,
                                                      const glm::mat4& worldTransform) {
    const glm::mat3 rot = glm::mat3(worldTransform);
    // KHR_lights_punctual: lights emit along the node's local −Z.
    const glm::vec3 emission =
        glm::normalize(glm::vec3(-rot[2])); // world −Z column

    if (light.type == gfx::LightType::Directional) {
        // Shader L (to-light) = −emission = world +Z of the node.
        light.direction = glm::normalize(rot[2]);
        light.position = glm::vec3(0.0f);
    } else if (light.type == gfx::LightType::Point) {
        // Position only (rotation/scale ignored per KHR for position).
        light.position = glm::vec3(worldTransform[3]);
    } else {
        // Spot: position + emission direction for the cone.
        light.position = glm::vec3(worldTransform[3]);
        light.direction = emission;
    }
}

std::vector<gfx::MeshPrimitiveID>
scene::GltfLoader::extract_mesh_data(const tinygltf::Model &model,
                                    gfx::Renderer &renderer) {

    std::vector<gfx::MeshPrimitiveID> meshes{};
    meshes.reserve(model.meshes.size() *
                   2); // most glTF meshes have multiple primitives

    for (const auto &mesh : model.meshes) {
        for (const auto &primitive : mesh.primitives) {
            // glTF default primitive mode is TRIANGLES (4) when omitted (many minimal
            // test assets like Cameras.gltf do not specify "mode").
            int mode = (primitive.mode >= 0) ? primitive.mode : TINYGLTF_MODE_TRIANGLES;
            if (mode != TINYGLTF_MODE_TRIANGLES)
                continue;

            // POSITION is mandatory for anything we can render. NORMAL and TEXCOORD_0
            // are optional: we synthesize flat normal (0,0,1) and zero UVs so that
            // minimal test scenes (pure camera tests, simple proxy geo) still load.
            auto pos_it = primitive.attributes.find("POSITION");
            if (pos_it == primitive.attributes.end())
                continue;

            const auto &pos_acc = model.accessors[pos_it->second];
            if (pos_acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
                pos_acc.type != TINYGLTF_TYPE_VEC3)
                continue;

            auto norm_it = primitive.attributes.find("NORMAL");
            auto tex_it = primitive.attributes.find("TEXCOORD_0");

            bool has_normal = (norm_it != primitive.attributes.end());
            bool has_tex0   = (tex_it != primitive.attributes.end());

            const tinygltf::Accessor *norm_acc_ptr = nullptr;
            const tinygltf::Accessor *tex_acc_ptr  = nullptr;

            if (has_normal) {
                norm_acc_ptr = &model.accessors[norm_it->second];
                if (norm_acc_ptr->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
                    norm_acc_ptr->type != TINYGLTF_TYPE_VEC3) {
                    has_normal = false;
                    norm_acc_ptr = nullptr;
                }
            }

            if (has_tex0) {
                tex_acc_ptr = &model.accessors[tex_it->second];
                bool texcoord_ok =
                    (tex_acc_ptr->type == TINYGLTF_TYPE_VEC2) &&
                    (tex_acc_ptr->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT ||
                     tex_acc_ptr->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ||
                     tex_acc_ptr->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                     tex_acc_ptr->componentType == TINYGLTF_COMPONENT_TYPE_SHORT ||
                     tex_acc_ptr->componentType == TINYGLTF_COMPONENT_TYPE_BYTE);
                if (!texcoord_ok) {
                    has_tex0 = false;
                    tex_acc_ptr = nullptr;
                }
            }

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
            const uint8_t *norm_ptr = has_normal && norm_acc_ptr ? get_accessor_data(*norm_acc_ptr) : nullptr;

            int pos_stride =
                pos_acc.ByteStride(model.bufferViews[pos_acc.bufferView]);
            int norm_stride = 12;
            if (has_normal && norm_acc_ptr) {
                norm_stride = norm_acc_ptr->ByteStride(model.bufferViews[norm_acc_ptr->bufferView]);
                if (norm_stride == 0) norm_stride = 12;
            }

            if (pos_stride == 0)
                pos_stride = 12; // vec3 float

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

                // Normal (required by Vertex; default to +Z if absent in glTF)
                if (has_normal && norm_ptr) {
                    const float *n = reinterpret_cast<const float *>(
                        norm_ptr + i * norm_stride);
                    v.normal[0] = n[0];
                    v.normal[1] = n[1];
                    v.normal[2] = n[2];
                } else {
                    v.normal[0] = 0.0f;
                    v.normal[1] = 0.0f;
                    v.normal[2] = 1.0f;
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
                    glm::vec2 t{0.0f, 0.0f};
                    if (has_tex0 && tex_acc_ptr) {
                        // Use the helper that properly handles normalized integer UVs
                        t = GetTexcoordFromAccessor(model, *tex_acc_ptr, i);
                    }
                    // else: leave at (0,0) — fine for untextured proxy geometry (e.g. Cameras.gltf test plane)

                    // Use fractional part so UVs outside [0,1] (tiling / repeat) are preserved
                    // instead of being smashed to the texture edge.
                    float u_frac = t.x - std::floor(t.x);
                    float v_frac = t.y - std::floor(t.y);

                    uint16_t u = static_cast<uint16_t>(
                        std::clamp(u_frac, 0.0f, 1.0f) * 65535.0f);
                    uint16_t vval = static_cast<uint16_t>(
                        std::clamp(v_frac, 0.0f, 1.0f) * 65535.0f);

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

            meshes.push_back(renderer.mesh_manager.add_mesh(mesh_data));
        }
    }
    return meshes;
}

glm::mat4 scene::GltfLoader::extract_node_transform(const tinygltf::Node &node) {
    // glTF nodes may specify either a 4x4 matrix or separate TRS.
    // Matrix takes precedence when present and has 16 elements.
    if (node.matrix.size() == 16) {
        // tinygltf stores column-major (matches glm default)
        return glm::make_mat4(node.matrix.data());
    }

    // glTF spec: absent TRS components mean identity (trans=0, rot=unit quat, scale=1).
    // The old value_or_ident always supplied 1.0 which was wrong for translation
    // (and for quaternion when rotation key omitted). Fixed to be glTF-compliant.
    glm::vec3 t(0.0f);
    if (node.translation.size() >= 3) {
        t = glm::vec3(static_cast<float>(node.translation[0]),
                      static_cast<float>(node.translation[1]),
                      static_cast<float>(node.translation[2]));
    }

    glm::quat r(1.0f, 0.0f, 0.0f, 0.0f); // identity (w,x,y,z)
    if (node.rotation.size() >= 4) {
        // glTF stores rotation as [x, y, z, w]; glm::quat(w, x, y, z)
        float rx = static_cast<float>(node.rotation[0]);
        float ry = static_cast<float>(node.rotation[1]);
        float rz = static_cast<float>(node.rotation[2]);
        float rw = static_cast<float>(node.rotation[3]);
        r = glm::quat(rw, rx, ry, rz);
    }

    glm::vec3 s(1.0f);
    if (node.scale.size() >= 3) {
        s = glm::vec3(static_cast<float>(node.scale[0]),
                      static_cast<float>(node.scale[1]),
                      static_cast<float>(node.scale[2]));
    }

    return glm::translate(glm::mat4(1.0f), t) *
           glm::mat4_cast(r) *
           glm::scale(glm::mat4(1.0f), s);
}

