#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "scene/GltfLoader.h"
#include "core/Handle.h"
#include "gfx/MeshData.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <iostream>
#include <map>
#include <tiny_gltf.h>

// glTF COLOR_0: VEC3 or VEC4, float or normalized integer → RGBA in 0..1.
static glm::vec4 GetColorFromAccessor(const tinygltf::Model& model,
                                      const tinygltf::Accessor& acc,
                                      size_t index) {
    glm::vec4 c(1.0f, 1.0f, 1.0f, 1.0f);
    if (acc.bufferView < 0)
        return c;
    if (acc.type != TINYGLTF_TYPE_VEC3 && acc.type != TINYGLTF_TYPE_VEC4)
        return c;

    const auto& bv = model.bufferViews[acc.bufferView];
    if (bv.buffer < 0 || bv.buffer >= (int)model.buffers.size())
        return c;

    const auto& buf = model.buffers[bv.buffer];
    const uint8_t* base = buf.data.data() + bv.byteOffset + acc.byteOffset;
    int stride = acc.ByteStride(bv);
    if (stride == 0) {
        stride = tinygltf::GetNumComponentsInType(acc.type) *
                 tinygltf::GetComponentSizeInBytes(acc.componentType);
    }
    const uint8_t* ptr = base + index * stride;
    const int ncomp = (acc.type == TINYGLTF_TYPE_VEC4) ? 4 : 3;

    auto pack_channel = [](float x) -> float {
        return std::clamp(x, 0.0f, 1.0f);
    };

    switch (acc.componentType) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT: {
        const float* f = reinterpret_cast<const float*>(ptr);
        c.r = pack_channel(f[0]);
        c.g = pack_channel(f[1]);
        c.b = pack_channel(f[2]);
        if (ncomp >= 4)
            c.a = pack_channel(f[3]);
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
        // glTF integer colors are normalized when accessor.normalized is true;
        // treat as unorm regardless (common authoring).
        c.r = float(ptr[0]) / 255.0f;
        c.g = float(ptr[1]) / 255.0f;
        c.b = float(ptr[2]) / 255.0f;
        if (ncomp >= 4)
            c.a = float(ptr[3]) / 255.0f;
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        const uint16_t* s = reinterpret_cast<const uint16_t*>(ptr);
        c.r = float(s[0]) / 65535.0f;
        c.g = float(s[1]) / 65535.0f;
        c.b = float(s[2]) / 65535.0f;
        if (ncomp >= 4)
            c.a = float(s[3]) / 65535.0f;
        break;
    }
    default:
        break;
    }
    return c;
}

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

    // Resolve a glTF texture index to a TextureManager handle.
    // Supports external files (.gltf), data-URI / bufferView embeds (.glb), and
    // pixels already decoded by tinygltf into image.image.
    auto resolve_texture = [&](int texture_index,
                               bool is_srgb) -> gfx::TextureID {
        if (texture_index < 0 || texture_index >= (int)model.textures.size())
            return gfx::TextureID{};
        const auto& tex = model.textures[texture_index];
        if (tex.source < 0 || tex.source >= (int)model.images.size())
            return gfx::TextureID{};
        const auto& img = model.images[static_cast<size_t>(tex.source)];
        const std::string key =
            !img.name.empty()
                ? img.name
                : ("gltf_img_" + std::to_string(tex.source));

        // External file URI (not data:).
        if (!img.uri.empty() && img.uri.rfind("data:", 0) != 0) {
            const auto uri_path = (texture_path / img.uri).lexically_normal();
            return renderer.texture_manager.get_texture_handle(
                key, uri_path.string(), is_srgb);
        }

        // tinygltf already decoded (common for .glb after LoadBinaryFromFile).
        if (img.width > 0 && img.height > 0 && !img.image.empty()) {
            return renderer.texture_manager.get_texture_handle_from_pixels(
                key, img.image.data(), img.width, img.height, img.component,
                is_srgb);
        }

        // Raw bufferView blob (PNG/JPEG/…) — decode with stb.
        if (img.bufferView >= 0 &&
            img.bufferView < static_cast<int>(model.bufferViews.size())) {
            const auto& bv =
                model.bufferViews[static_cast<size_t>(img.bufferView)];
            if (bv.buffer >= 0 &&
                bv.buffer < static_cast<int>(model.buffers.size())) {
                const auto& buf = model.buffers[static_cast<size_t>(bv.buffer)];
                // Image data starts at bufferView.byteOffset.
                const size_t start = static_cast<size_t>(bv.byteOffset);
                const size_t len = static_cast<size_t>(bv.byteLength);
                if (start + len <= buf.data.size()) {
                    return renderer.texture_manager.get_texture_handle_from_encoded(
                        key, buf.data.data() + start, len, is_srgb);
                }
            }
        }

        std::cerr << "[GLTF] Warning: could not resolve texture source "
                  << tex.source << " ('" << key << "') — skipping.\n";
        return gfx::TextureID{};
    };

    // Apply texCoord + KHR_texture_transform from a tinygltf texture info.
    auto apply_tex_info = [](gfx::Material& material, uint32_t slot, int tex_coord,
                             const tinygltf::ExtensionMap& extensions) {
        int set = tex_coord >= 0 ? tex_coord : 0;
        float scale_u = 1.f, scale_v = 1.f, off_u = 0.f, off_v = 0.f, rot = 0.f;
        auto it = extensions.find("KHR_texture_transform");
        if (it != extensions.end() && it->second.IsObject()) {
            const auto& obj = it->second.Get<tinygltf::Value::Object>();
            auto get_num = [&](const char* key, float def) -> float {
                auto jt = obj.find(key);
                if (jt == obj.end())
                    return def;
                if (jt->second.IsNumber())
                    return static_cast<float>(jt->second.GetNumberAsDouble());
                return def;
            };
            auto get_vec2 = [&](const char* key, float& x, float& y) {
                auto jt = obj.find(key);
                if (jt == obj.end() || !jt->second.IsArray())
                    return;
                const auto& arr = jt->second.Get<tinygltf::Value::Array>();
                if (arr.size() >= 1 && arr[0].IsNumber())
                    x = static_cast<float>(arr[0].GetNumberAsDouble());
                if (arr.size() >= 2 && arr[1].IsNumber())
                    y = static_cast<float>(arr[1].GetNumberAsDouble());
            };
            get_vec2("scale", scale_u, scale_v);
            get_vec2("offset", off_u, off_v);
            rot = get_num("rotation", 0.f);
            // Extension may override texCoord
            auto jt = obj.find("texCoord");
            if (jt != obj.end() && jt->second.IsNumber())
                set = static_cast<int>(jt->second.GetNumberAsInt());
        }
        material.set_texcoord(slot, static_cast<uint32_t>(std::max(0, set)));
        material.set_uv_transform(slot, scale_u, scale_v, off_u, off_v, rot);
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
        material.alpha_cutoff = 0.5f;

        // --- PBR base values (glTF 2.0 defaults) ---
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

        // Factors multiply textures in the shader when maps are present.
        material.metallic  = static_cast<float>(pbr.metallicFactor);
        material.roughness = static_cast<float>(pbr.roughnessFactor);
        material.normalStrength = 1.0f;

        // --- Textures (file URI, data URI, or .glb bufferView embed) ---
        {
            const auto& info = pbr.baseColorTexture;
            if (info.index >= 0) {
                const gfx::TextureID id =
                    resolve_texture(info.index, /*is_srgb=*/true);
                if (id.value != core::INVALID_HANDLE) {
                    material.albedo_texture_index = id;
                    material.flags |= gfx::Material::kFlagHasAlbedoTex;
                    apply_tex_info(material, gfx::Material::kUvAlbedo,
                                   info.texCoord, info.extensions);
                }
            }
        }
        {
            const auto& info = pbr.metallicRoughnessTexture;
            if (info.index >= 0) {
                const gfx::TextureID id =
                    resolve_texture(info.index, /*is_srgb=*/false);
                if (id.value != core::INVALID_HANDLE) {
                    material.roughness_texture_index = id;
                    material.flags |= gfx::Material::kFlagHasOrmTex;
                    apply_tex_info(material, gfx::Material::kUvOrm, info.texCoord,
                                   info.extensions);
                }
            }
        }
        {
            const auto& info = mat.normalTexture;
            if (info.index >= 0) {
                const gfx::TextureID id =
                    resolve_texture(info.index, /*is_srgb=*/false);
                if (id.value != core::INVALID_HANDLE) {
                    material.normal_texture_index = id;
                    material.flags |= gfx::Material::kFlagHasNormalMap;
                    if (info.scale != 0.0)
                        material.normalStrength = static_cast<float>(info.scale);
                    apply_tex_info(material, gfx::Material::kUvNormal,
                                   info.texCoord, info.extensions);
                }
            }
        }
        {
            const auto& info = mat.emissiveTexture;
            if (info.index >= 0) {
                const gfx::TextureID id =
                    resolve_texture(info.index, /*is_srgb=*/true);
                if (id.value != core::INVALID_HANDLE) {
                    material.emissive_texture_index = id;
                    material.flags |= gfx::Material::kFlagHasEmissiveTex;
                    apply_tex_info(material, gfx::Material::kUvEmissive,
                                   info.texCoord, info.extensions);
                }
            }
        }
        {
            const auto& info = mat.occlusionTexture;
            if (info.index >= 0) {
                const gfx::TextureID id =
                    resolve_texture(info.index, /*is_srgb=*/false);
                if (id.value != core::INVALID_HANDLE) {
                    material.ao_texture_index = id;
                    apply_tex_info(material, gfx::Material::kUvAo, info.texCoord,
                                   info.extensions);
                }
            }
        }

        // Emissive factor (RGB) + KHR_materials_emissive_strength
        material.emissive_factor = glm::vec4(0.f, 0.f, 0.f, 1.f);
        if (mat.emissiveFactor.size() >= 3) {
            material.emissive_factor = glm::vec4(
                static_cast<float>(mat.emissiveFactor[0]),
                static_cast<float>(mat.emissiveFactor[1]),
                static_cast<float>(mat.emissiveFactor[2]), 1.f);
        }
        {
            auto it = mat.extensions.find("KHR_materials_emissive_strength");
            if (it != mat.extensions.end() && it->second.IsObject()) {
                const auto& obj = it->second.Get<tinygltf::Value::Object>();
                auto jt = obj.find("emissiveStrength");
                if (jt != obj.end() && jt->second.IsNumber())
                    material.emissive_factor.w =
                        static_cast<float>(jt->second.GetNumberAsDouble());
            }
        }
        if (glm::length(glm::vec3(material.emissive_factor)) > 1e-6f ||
            (material.flags & gfx::Material::kFlagHasEmissiveTex))
            material.flags |= gfx::Material::kFlagIsEmissive;

        // KHR_materials_clearcoat (factors only — CarConcept has no clearcoat maps)
        {
            auto it = mat.extensions.find("KHR_materials_clearcoat");
            if (it != mat.extensions.end() && it->second.IsObject()) {
                const auto& obj = it->second.Get<tinygltf::Value::Object>();
                auto num = [&](const char* k, float def) {
                    auto jt = obj.find(k);
                    if (jt != obj.end() && jt->second.IsNumber())
                        return static_cast<float>(jt->second.GetNumberAsDouble());
                    return def;
                };
                material.clearcoat = num("clearcoatFactor", 0.f);
                material.clearcoat_roughness = num("clearcoatRoughnessFactor", 0.f);
                if (material.clearcoat > 1e-4f)
                    material.flags |= gfx::Material::kFlagClearcoat;
            }
        }

        // KHR_materials_transmission (MVP: factor only, no refraction pass)
        {
            auto it = mat.extensions.find("KHR_materials_transmission");
            if (it != mat.extensions.end() && it->second.IsObject()) {
                const auto& obj = it->second.Get<tinygltf::Value::Object>();
                auto jt = obj.find("transmissionFactor");
                if (jt != obj.end() && jt->second.IsNumber())
                    material.transmission =
                        static_cast<float>(jt->second.GetNumberAsDouble());
                if (material.transmission > 1e-4f) {
                    material.flags |= gfx::Material::kFlagTransmission;
                    // Glass-like: force blend so background can show through a bit
                    material.flags |= gfx::Material::kFlagAlphaBlend;
                }
            }
        }

        // KHR_materials_iridescence (simplified single-thickness thin-film tint)
        {
            auto it = mat.extensions.find("KHR_materials_iridescence");
            if (it != mat.extensions.end() && it->second.IsObject()) {
                const auto& obj = it->second.Get<tinygltf::Value::Object>();
                auto num = [&](const char* k, float def) {
                    auto jt = obj.find(k);
                    if (jt != obj.end() && jt->second.IsNumber())
                        return static_cast<float>(jt->second.GetNumberAsDouble());
                    return def;
                };
                material.iridescence = num("iridescenceFactor", 0.f);
                material.iridescence_ior = num("iridescenceIor", 1.3f);
                const float tmin = num("iridescenceThicknessMinimum", 100.f);
                const float tmax = num("iridescenceThicknessMaximum", 400.f);
                material.iridescence_thickness = 0.5f * (tmin + tmax);
                if (material.iridescence > 1e-4f)
                    material.flags |= gfx::Material::kFlagIridescence;
            }
        }

        // glTF alphaMode
        if (mat.alphaMode == "MASK") {
            material.flags |= gfx::Material::kFlagAlphaMask;
            material.alpha_cutoff =
                (mat.alphaCutoff > 0.0) ? static_cast<float>(mat.alphaCutoff) : 0.5f;
        } else if (mat.alphaMode == "BLEND") {
            material.flags |= gfx::Material::kFlagAlphaBlend;
        }

        if (mat.doubleSided)
            material.flags |= gfx::Material::kFlagDoubleSided;

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
            auto tex1_it = primitive.attributes.find("TEXCOORD_1");
            auto color_it = primitive.attributes.find("COLOR_0");
            auto joints_it = primitive.attributes.find("JOINTS_0");
            auto weights_it = primitive.attributes.find("WEIGHTS_0");

            bool has_normal = (norm_it != primitive.attributes.end());
            bool has_tex0   = (tex_it != primitive.attributes.end());
            bool has_tex1   = (tex1_it != primitive.attributes.end());
            bool has_color  = (color_it != primitive.attributes.end());
            bool has_joints = (joints_it != primitive.attributes.end());
            bool has_weights = (weights_it != primitive.attributes.end());

            const tinygltf::Accessor *norm_acc_ptr = nullptr;
            const tinygltf::Accessor *tex_acc_ptr  = nullptr;
            const tinygltf::Accessor *tex1_acc_ptr = nullptr;
            const tinygltf::Accessor *color_acc_ptr = nullptr;
            const tinygltf::Accessor *joints_acc_ptr = nullptr;
            const tinygltf::Accessor *weights_acc_ptr = nullptr;

            auto texcoord_accessor_ok = [](const tinygltf::Accessor& acc) {
                return (acc.type == TINYGLTF_TYPE_VEC2) &&
                       (acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT ||
                        acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ||
                        acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                        acc.componentType == TINYGLTF_COMPONENT_TYPE_SHORT ||
                        acc.componentType == TINYGLTF_COMPONENT_TYPE_BYTE);
            };

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
                if (!texcoord_accessor_ok(*tex_acc_ptr)) {
                    has_tex0 = false;
                    tex_acc_ptr = nullptr;
                }
            }
            if (has_tex1) {
                tex1_acc_ptr = &model.accessors[tex1_it->second];
                if (!texcoord_accessor_ok(*tex1_acc_ptr)) {
                    has_tex1 = false;
                    tex1_acc_ptr = nullptr;
                }
            }

            if (has_color) {
                color_acc_ptr = &model.accessors[color_it->second];
                const bool color_ok =
                    (color_acc_ptr->type == TINYGLTF_TYPE_VEC3 ||
                     color_acc_ptr->type == TINYGLTF_TYPE_VEC4) &&
                    (color_acc_ptr->componentType ==
                         TINYGLTF_COMPONENT_TYPE_FLOAT ||
                     color_acc_ptr->componentType ==
                         TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                     color_acc_ptr->componentType ==
                         TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT);
                if (!color_ok) {
                    has_color = false;
                    color_acc_ptr = nullptr;
                }
            }

            if (has_joints) {
                joints_acc_ptr = &model.accessors[joints_it->second];
                if (joints_acc_ptr->type != TINYGLTF_TYPE_VEC4 ||
                    (joints_acc_ptr->componentType !=
                         TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
                     joints_acc_ptr->componentType !=
                         TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)) {
                    has_joints = false;
                    joints_acc_ptr = nullptr;
                }
            }
            if (has_weights) {
                weights_acc_ptr = &model.accessors[weights_it->second];
                if (weights_acc_ptr->type != TINYGLTF_TYPE_VEC4 ||
                    weights_acc_ptr->componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
                    // Also allow normalized u8 weights if present
                    if (!(weights_acc_ptr->type == TINYGLTF_TYPE_VEC4 &&
                          weights_acc_ptr->componentType ==
                              TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)) {
                        has_weights = false;
                        weights_acc_ptr = nullptr;
                    }
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
            const uint8_t *joints_ptr =
                has_joints && joints_acc_ptr ? get_accessor_data(*joints_acc_ptr) : nullptr;
            const uint8_t *weights_ptr =
                has_weights && weights_acc_ptr ? get_accessor_data(*weights_acc_ptr)
                                               : nullptr;

            int pos_stride =
                pos_acc.ByteStride(model.bufferViews[pos_acc.bufferView]);
            int norm_stride = 12;
            if (has_normal && norm_acc_ptr) {
                norm_stride = norm_acc_ptr->ByteStride(model.bufferViews[norm_acc_ptr->bufferView]);
                if (norm_stride == 0) norm_stride = 12;
            }
            int joints_stride = 0;
            if (has_joints && joints_acc_ptr) {
                joints_stride = joints_acc_ptr->ByteStride(
                    model.bufferViews[joints_acc_ptr->bufferView]);
                if (joints_stride == 0) {
                    joints_stride =
                        (joints_acc_ptr->componentType ==
                         TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                            ? 8
                            : 4;
                }
            }
            int weights_stride = 0;
            if (has_weights && weights_acc_ptr) {
                weights_stride = weights_acc_ptr->ByteStride(
                    model.bufferViews[weights_acc_ptr->bufferView]);
                if (weights_stride == 0) {
                    weights_stride =
                        (weights_acc_ptr->componentType ==
                         TINYGLTF_COMPONENT_TYPE_FLOAT)
                            ? 16
                            : 4;
                }
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

                // UV0 + UV1 as IEEE half floats (R16G16B16A16_SFLOAT vertex attr).
                // Do NOT frac/unorm-pack: License plate UVs sit slightly outside [0,1]
                // (e.g. V≈1.04); frac wrapping maps them to the wrong end of Khronos_C.
                // Half retains full range for tiling UVs and slight OOB clamp/wrap cases.
                auto pack_uv_half = [](const glm::vec2& t, uint8_t* dst) {
                    const uint32_t packed = glm::packHalf2x16(t);
                    std::memcpy(dst, &packed, sizeof(packed));
                };

                glm::vec2 uv0{0.0f, 0.0f};
                if (has_tex0 && tex_acc_ptr)
                    uv0 = GetTexcoordFromAccessor(model, *tex_acc_ptr, i);
                pack_uv_half(uv0, v.uv);

                glm::vec2 uv1 = uv0; // default: duplicate UV0 if TEXCOORD_1 absent
                if (has_tex1 && tex1_acc_ptr)
                    uv1 = GetTexcoordFromAccessor(model, *tex1_acc_ptr, i);
                pack_uv_half(uv1, v.uv + 4);

                // COLOR_0 → RGBA8 unorm (default white)
                v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
                if (has_color && color_acc_ptr) {
                    const glm::vec4 c =
                        GetColorFromAccessor(model, *color_acc_ptr, i);
                    v.color[0] = static_cast<uint8_t>(
                        std::clamp(c.r, 0.0f, 1.0f) * 255.0f + 0.5f);
                    v.color[1] = static_cast<uint8_t>(
                        std::clamp(c.g, 0.0f, 1.0f) * 255.0f + 0.5f);
                    v.color[2] = static_cast<uint8_t>(
                        std::clamp(c.b, 0.0f, 1.0f) * 255.0f + 0.5f);
                    v.color[3] = static_cast<uint8_t>(
                        std::clamp(c.a, 0.0f, 1.0f) * 255.0f + 0.5f);
                }

                // Skinning: JOINTS_0 + WEIGHTS_0 (up to 4 influences)
                v.blend_weights[0] = 255;
                v.blend_weights[1] = v.blend_weights[2] = v.blend_weights[3] = 0;
                v.blend_indices[0] = v.blend_indices[1] = v.blend_indices[2] =
                    v.blend_indices[3] = 0;

                if (has_joints && joints_ptr && joints_acc_ptr) {
                    const uint8_t* jp = joints_ptr + i * joints_stride;
                    if (joints_acc_ptr->componentType ==
                        TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        for (int k = 0; k < 4; ++k)
                            v.blend_indices[k] = jp[k];
                    } else {
                        const uint16_t* js = reinterpret_cast<const uint16_t*>(jp);
                        for (int k = 0; k < 4; ++k)
                            v.blend_indices[k] =
                                static_cast<uint8_t>(std::min<uint16_t>(js[k], 255));
                    }
                }
                if (has_weights && weights_ptr && weights_acc_ptr) {
                    float w[4] = {1.f, 0.f, 0.f, 0.f};
                    const uint8_t* wp = weights_ptr + i * weights_stride;
                    if (weights_acc_ptr->componentType ==
                        TINYGLTF_COMPONENT_TYPE_FLOAT) {
                        const float* wf = reinterpret_cast<const float*>(wp);
                        for (int k = 0; k < 4; ++k)
                            w[k] = wf[k];
                    } else {
                        for (int k = 0; k < 4; ++k)
                            w[k] = float(wp[k]) / 255.0f;
                    }
                    float sum = w[0] + w[1] + w[2] + w[3];
                    if (sum > 1e-6f) {
                        for (int k = 0; k < 4; ++k)
                            w[k] /= sum;
                    } else {
                        w[0] = 1.f;
                        w[1] = w[2] = w[3] = 0.f;
                    }
                    for (int k = 0; k < 4; ++k) {
                        v.blend_weights[k] = static_cast<uint8_t>(
                            std::clamp(w[k], 0.0f, 1.0f) * 255.0f + 0.5f);
                    }
                }
            }

            // Index buffer. glTF allows non-indexed primitives (e.g. Fox) —
            // synthesize 0..N-1 so the rest of the pipeline can stay indexed.
            if (primitive.indices >= 0) {
                const auto &idx_acc = model.accessors[primitive.indices];
                const int ctype = idx_acc.componentType;
                const bool ok_type =
                    ctype == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                    ctype == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ||
                    ctype == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
                if (ok_type && idx_acc.bufferView >= 0) {
                    const uint8_t *idx_ptr = get_accessor_data(idx_acc);
                    int idx_stride = idx_acc.ByteStride(
                        model.bufferViews[idx_acc.bufferView]);
                    if (idx_stride == 0) {
                        idx_stride =
                            tinygltf::GetComponentSizeInBytes(ctype);
                    }

                    mesh_data.indices.resize(idx_acc.count);
                    for (size_t i = 0; i < idx_acc.count; ++i) {
                        const uint8_t* ip = idx_ptr + i * static_cast<size_t>(idx_stride);
                        uint32_t v = 0;
                        switch (ctype) {
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                            v = ip[0];
                            break;
                        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                            v = reinterpret_cast<const uint16_t*>(ip)[0];
                            break;
                        default:
                            v = reinterpret_cast<const uint32_t*>(ip)[0];
                            break;
                        }
                        mesh_data.indices[i] = v;
                    }
                }
            }
            if (mesh_data.indices.empty() && num_vertices >= 3) {
                // Non-indexed TRIANGLES (default mode): sequential indices.
                // mode 4 = TRIANGLES; other modes rare in our samples.
                mesh_data.indices.resize(num_vertices);
                for (size_t i = 0; i < num_vertices; ++i)
                    mesh_data.indices[i] = static_cast<uint32_t>(i);
            }

            // Assets like Fox omit NORMAL — accumulate face normals for lit shading.
            if (!has_normal && mesh_data.indices.size() >= 3) {
                std::vector<glm::vec3> accum(num_vertices, glm::vec3(0.0f));
                for (size_t t = 0; t + 2 < mesh_data.indices.size(); t += 3) {
                    const uint32_t i0 = mesh_data.indices[t];
                    const uint32_t i1 = mesh_data.indices[t + 1];
                    const uint32_t i2 = mesh_data.indices[t + 2];
                    if (i0 >= num_vertices || i1 >= num_vertices ||
                        i2 >= num_vertices)
                        continue;
                    const glm::vec3 p0(mesh_data.vertices[i0].position[0],
                                       mesh_data.vertices[i0].position[1],
                                       mesh_data.vertices[i0].position[2]);
                    const glm::vec3 p1(mesh_data.vertices[i1].position[0],
                                       mesh_data.vertices[i1].position[1],
                                       mesh_data.vertices[i1].position[2]);
                    const glm::vec3 p2(mesh_data.vertices[i2].position[0],
                                       mesh_data.vertices[i2].position[1],
                                       mesh_data.vertices[i2].position[2]);
                    const glm::vec3 fn = glm::cross(p1 - p0, p2 - p0);
                    accum[i0] += fn;
                    accum[i1] += fn;
                    accum[i2] += fn;
                }
                for (size_t i = 0; i < num_vertices; ++i) {
                    glm::vec3 n = accum[i];
                    const float len2 = glm::dot(n, n);
                    if (len2 > 1e-12f)
                        n *= 1.0f / std::sqrt(len2);
                    else
                        n = glm::vec3(0.0f, 1.0f, 0.0f);
                    mesh_data.vertices[i].normal[0] = n.x;
                    mesh_data.vertices[i].normal[1] = n.y;
                    mesh_data.vertices[i].normal[2] = n.z;
                }
            }

            mesh_data.local_aabb = aabb;

            meshes.push_back(renderer.mesh_manager.add_mesh(mesh_data));
        }
    }
    return meshes;
}

scene::LocalTrs scene::GltfLoader::extract_node_trs(const tinygltf::Node &node) {
    LocalTrs trs{};

    if (node.matrix.size() == 16) {
        // Matrix form: store composed matrix path via decompose for anim base.
        const glm::mat4 m = glm::make_mat4(node.matrix.data());
        trs.translation = glm::vec3(m[3]);
        glm::vec3 col0(m[0]), col1(m[1]), col2(m[2]);
        trs.scale = glm::vec3(glm::length(col0), glm::length(col1), glm::length(col2));
        if (trs.scale.x > 1e-8f)
            col0 /= trs.scale.x;
        if (trs.scale.y > 1e-8f)
            col1 /= trs.scale.y;
        if (trs.scale.z > 1e-8f)
            col2 /= trs.scale.z;
        trs.rotation = glm::normalize(glm::quat_cast(glm::mat3(col0, col1, col2)));
        return trs;
    }

    if (node.translation.size() >= 3) {
        trs.translation = glm::vec3(static_cast<float>(node.translation[0]),
                                    static_cast<float>(node.translation[1]),
                                    static_cast<float>(node.translation[2]));
    }
    if (node.rotation.size() >= 4) {
        // glTF [x,y,z,w] → glm(w,x,y,z)
        trs.rotation = glm::quat(static_cast<float>(node.rotation[3]),
                                 static_cast<float>(node.rotation[0]),
                                 static_cast<float>(node.rotation[1]),
                                 static_cast<float>(node.rotation[2]));
        trs.rotation = glm::normalize(trs.rotation);
    }
    if (node.scale.size() >= 3) {
        trs.scale = glm::vec3(static_cast<float>(node.scale[0]),
                              static_cast<float>(node.scale[1]),
                              static_cast<float>(node.scale[2]));
    }
    return trs;
}

glm::mat4 scene::GltfLoader::extract_node_transform(const tinygltf::Node &node) {
    if (node.matrix.size() == 16)
        return glm::make_mat4(node.matrix.data());
    const LocalTrs trs = extract_node_trs(node);
    return glm::translate(glm::mat4(1.0f), trs.translation) *
           glm::mat4_cast(trs.rotation) *
           glm::scale(glm::mat4(1.0f), trs.scale);
}

