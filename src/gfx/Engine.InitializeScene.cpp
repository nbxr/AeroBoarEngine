#include "gfx/Engine.h"
#include "gfx/BufferUtils.h"
#include "gfx/Renderer.h"
#include "gfx/TextureManager.h"
#include "gfx/DrawBatch.h"
#include "core/Configuration.h"
#include "core/Log.h"
#include "scene/GltfLoader.h"
#include "scene/SceneManager.h"
#include "tiny_gltf.h"
#include <functional>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <glm/gtc/quaternion.hpp>  // for mat3_cast in pointing debug

bool gfx::Engine::load_default_scene() {
    const auto &config = core::Configuration::get_instance();
    if (!config.is_loaded()) {
        std::cerr << "Configuration has not been loaded" << std::endl;
        return false;
    }

    const std::string default_scene = config.find<std::string>("defaultScene");
    if (default_scene.empty()) {
        std::cerr << "defaultScene not found in configuration.json"
                  << std::endl;
        return false;
    }

    return load_scene(default_scene);
}

bool gfx::Engine::load_scene(const std::string &scene_name) {
    const auto &config = core::Configuration::get_instance();
    if (!config.is_loaded()) {
        std::cerr << "Configuration has not been loaded" << std::endl;
        return false;
    }

    const nlohmann::json &root = core::Configuration::get_root();

    std::string active_system = config.find<std::string>("activeSystem");
    if (active_system.empty()) {
        active_system = "Windows";
    }

    std::string home_path;
    if (root.contains("home") && root["home"].is_array()) {
        for (const auto &entry : root["home"]) {
            if (entry.contains("system") && entry["system"] == active_system &&
                entry.contains("path")) {
                home_path = entry["path"].get<std::string>();
                break;
            }
        }
    }

    if (home_path.empty()) {
        std::cerr << "No home path configured for activeSystem '" << active_system
                  << "' in configuration.json" << std::endl;
        return false;
    }

    if (!root.contains("scenes") || !root["scenes"].is_array()) {
        std::cerr << "scenes array not found in configuration.json" << std::endl;
        return false;
    }

    std::string filename;
    bool found_scene = false;
    for (const auto &scene : root["scenes"]) {
        if (scene.contains("name") && scene["name"] == scene_name &&
            scene.contains("filename")) {
            filename = scene["filename"].get<std::string>();
            found_scene = true;
            break;
        }
    }

    if (!found_scene) {
        std::cerr << "Scene name '" << scene_name
                  << "' not found in configuration.json" << std::endl;
        return false;
    }

    const std::string resolved_filename =
        (std::filesystem::path(home_path) / filename).make_preferred().string();

    // extract mesh data and create GPU buffers
    tinygltf::Model model{};
    if (!scene::GltfLoader::load_model(resolved_filename, model)) {
        return false;
    }

    // create materials in the material manager and get a lookup
    std::vector<MaterialID> material_lookup =
        scene::GltfLoader::extract_material_data(resolved_filename, model, renderer);

    // Ensure at least one material exists (e.g. minimal test scenes like Cameras.gltf
    // define geometry but no materials array, and primitives may omit "material").
    // The shader treats NO_TEXTURE indices by falling back to the factor values.
    if (material_lookup.empty()) {
        gfx::Material def{};
        def.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        def.roughness = 0.5f;
        def.metallic = 0.0f;
        def.emissive = 0.0f;
        def.normalStrength = 1.0f;
        def.albedo_texture_index = gfx::Material::NO_TEXTURE;
        def.normal_texture_index = gfx::Material::NO_TEXTURE;
        def.roughness_texture_index = gfx::Material::NO_TEXTURE;
        def.emissive_texture_index = gfx::Material::NO_TEXTURE;
        def.ao_texture_index = gfx::Material::NO_TEXTURE;
        def.sampler_index = gfx::Material::NO_TEXTURE;
        def.flags = 0;
        MaterialID def_id = renderer.material_manager.create_material(def);
        material_lookup.push_back(def_id);
    }

    // get meshes using lookup to store material ID on MeshData
    std::vector<MeshPrimitiveID> mesh_lookup =
        scene::GltfLoader::extract_mesh_data(model, renderer);

    // Extract KHR_lights_punctual lights (world transforms applied after traversal).
    renderer.lights = scene::GltfLoader::extract_light_data(model);

    // Engine fallback directional (used when the glTF has no KHR_lights_punctual).
    // positionOrDirection is the *to-light* vector (shader L for NdotL).
    // Prefer a slightly angled overhead sun so top surfaces light correctly.
    if (renderer.globalLight.type == gfx::LightType::Directional &&
        glm::length(renderer.globalLight.positionOrDirection) < 0.001f) {
        renderer.globalLight = {
            gfx::LightType::Directional,
            glm::normalize(glm::vec3(0.35f, 1.0f, 0.25f)),
            glm::vec3(1.0f, 0.98f, 0.95f),
            3.0f};
    }

    // calculate offsets for material lookup based on primitives
    std::vector<size_t> prim_material_offsets{};
    prim_material_offsets.reserve(model.meshes.size());
    size_t offset = 0;
    for (auto &mesh : model.meshes) {
        prim_material_offsets.push_back(offset);
        offset += mesh.primitives.size();
    }

    // Minimal glTF camera capture (only perspective for now)
    struct LoadedCameraInfo {
        glm::mat4 world_transform{1.0f};
        float yfov = glm::radians(60.0f);
        float aspectRatio = 0.0f;   // 0 = not specified in glTF (use runtime aspect)
        float znear = 0.1f;
        float zfar = 100.0f;
        bool valid = false;
        int   gltf_camera_index = -1;
    } loaded_camera{};

    // Safe GLTF traversal:
    // - Visit the full scene graph starting from the active scene's root nodes.
    // - Accumulate world transforms for *all* nodes (meshes, cameras, lights, etc.).
    // - Camera nodes are captured (first one found in DFS order) so we can
    //   drive the view from the authored camera.
    // - Light nodes (KHR_lights_punctual) now have their world transforms captured
    //   here so we can drive lighting from the scene (see post-traversal apply below).
    // - Mesh nodes produce GameObject + RenderMesh entries (TransformManager).
    // - Respect the scene graph: start from the default scene roots and
    //   accumulate world transforms via children.
    // - Guard against prim.material == -1 (default material) and out-of-range
    //   indices.
    std::vector<glm::mat4> light_world_transforms;
    if (!renderer.lights.empty()) {
        light_world_transforms.assign(renderer.lights.size(), glm::mat4(1.0f));
    }

    std::function<void(int, const glm::mat4 &, int)> add_mesh_node =
        [&](int node_idx, const glm::mat4 &parent_xform, int depth) {
            if (node_idx < 0 || node_idx >= (int)model.nodes.size())
                return;
            const auto &node = model.nodes[node_idx];
            glm::mat4 local = scene::GltfLoader::extract_node_transform(node);
            glm::mat4 world_xform = parent_xform * local;

            // Capture world transform for any light nodes so we can apply
            // position / direction from the authored node hierarchy (KHR_lights_punctual).
            if (node.light >= 0 && node.light < (int)light_world_transforms.size()) {
                light_world_transforms[node.light] = world_xform;
            }

            // Capture the first camera we encounter (minimal implementation)
            if (!loaded_camera.valid && node.camera >= 0 &&
                node.camera < (int)model.cameras.size()) {
                const auto &cam = model.cameras[node.camera];
                if (cam.type == "perspective") {
                    const auto &p = cam.perspective;
                    loaded_camera.world_transform = world_xform;
                    loaded_camera.yfov = (p.yfov > 0.0)
                                             ? static_cast<float>(p.yfov)
                                             : glm::radians(60.0f);
                    loaded_camera.aspectRatio = (p.aspectRatio > 0.0)
                                                    ? static_cast<float>(p.aspectRatio)
                                                    : 0.0f;
                    loaded_camera.znear =
                        (p.znear > 0.0) ? static_cast<float>(p.znear) : 0.1f;
                    loaded_camera.zfar =
                        (p.zfar > 0.0) ? static_cast<float>(p.zfar) : 100.0f;
                    loaded_camera.gltf_camera_index = node.camera;
                    loaded_camera.valid = true;
                }
                // Orthographic cameras are ignored in this minimal version
            }

            if (node.mesh >= 0 && node.mesh < (int)model.meshes.size()) {
                const size_t mesh_idx = static_cast<size_t>(node.mesh);
                size_t prim_i = 0;
                size_t mat_offset = prim_material_offsets[mesh_idx];

                // One GameObject per mesh node; all primitives share its root transform.
                const uint32_t go_id = renderer.scene_manager.create_game_object(
                    world_xform, static_cast<uint32_t>(node_idx));

                for (const auto &prim : model.meshes[mesh_idx].primitives) {
                    if (prim_i + mat_offset >= mesh_lookup.size())
                        break;

                    const uint32_t mesh_prim_id =
                        mesh_lookup[prim_i + mat_offset];
                    int mat_idx = prim.material;
                    if (mat_idx < 0 || mat_idx >= (int)material_lookup.size()) {
                        mat_idx = 0;
                    }
                    const uint32_t material_id = material_lookup[mat_idx];
                    core::AABB local_aabb =
                        renderer.mesh_manager.get_primitive_local_aabb(
                            mesh_prim_id);

                    renderer.scene_manager.add_render_mesh(
                        go_id, mesh_prim_id, material_id, local_aabb);
                    prim_i++;
                }
            }

            // Recurse into children with accumulated transform
            for (int child : node.children) {
                add_mesh_node(child, world_xform, depth + 1);
            }
        };

    // Choose the active scene and walk from its root nodes
    int active_scene = (model.defaultScene >= 0) ? model.defaultScene : 0;
    if (!model.scenes.empty() && active_scene < (int)model.scenes.size()) {
        for (int root_node : model.scenes[active_scene].nodes) {
            add_mesh_node(root_node, glm::mat4(1.0f), 0);
        }
    } else {
        // Fallback for malformed files: walk any node that has a mesh (still
        // safe)
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (model.nodes[i].mesh >= 0) {
                add_mesh_node(static_cast<int>(i), glm::mat4(1.0f), 0);
            }
        }
    }

    // Phase 2 lighting: apply collected world transforms to the extracted lights.
    // This makes KHR_lights_punctual lights appear in the correct world-space
    // locations/orientations authored in the DCC tool. Directional lights store
    // the engine's L vector (to-light) = node's local +Z in world (opposite the
    // emission direction per KHR spec: "emit light in the direction of the local -z axis").
    for (size_t i = 0; i < renderer.lights.size() && i < light_world_transforms.size(); ++i) {
        const glm::mat4& w = light_world_transforms[i];
        scene::GltfLoader::apply_world_transform_to_light(renderer.lights[i], w);
    }

    // Upload CPU data (populated by GltfLoader) into the persistently-mapped
    // GPU buffers on the "upload" side of each double-buffered manager.
    renderer.scene_manager.update_buffers();
    renderer.material_manager.update_buffers();
    renderer.mesh_manager.update_buffers();
    renderer.texture_manager.upload_textures();

    // Build instanced draw batches from RenderMesh + TransformManager
    // (GameObject scene model). Group by mesh_index for multi-instance draws.
    {
        renderer.draw_batches.clear();
        renderer.draw_instances_cpu.clear();
        gfx::BufferUtils::destroy_buffer(renderer.vk.device, renderer.allocator,
                                         renderer.draw_instance_buffer);

        const uint32_t n_rm = renderer.scene_manager.render_mesh_count();
        std::unordered_map<uint32_t, std::vector<uint32_t>> by_mesh;
        by_mesh.reserve(n_rm);
        for (uint32_t i = 0; i < n_rm; ++i) {
            const auto& rm = renderer.scene_manager.get_render_mesh(i);
            by_mesh[rm.mesh_index].push_back(i);
        }

        std::vector<uint32_t> mesh_keys;
        mesh_keys.reserve(by_mesh.size());
        for (const auto& [mesh_idx, ids] : by_mesh) {
            if (!ids.empty() &&
                renderer.mesh_manager.get_primitive_index_count(mesh_idx) > 0) {
                mesh_keys.push_back(mesh_idx);
            }
        }
        std::sort(mesh_keys.begin(), mesh_keys.end());

        renderer.draw_instances_cpu.reserve(n_rm);
        for (uint32_t mesh_idx : mesh_keys) {
            const auto& ids = by_mesh[mesh_idx];
            DrawBatch batch{};
            batch.mesh_index = mesh_idx;
            batch.index_count =
                renderer.mesh_manager.get_primitive_index_count(mesh_idx);
            batch.index_offset =
                renderer.mesh_manager.get_primitive_index_offset(mesh_idx);
            batch.vertex_offset = static_cast<int32_t>(
                renderer.mesh_manager.get_primitive_vertex_offset(mesh_idx));
            batch.first_instance =
                static_cast<uint32_t>(renderer.draw_instances_cpu.size());
            batch.instance_count = static_cast<uint32_t>(ids.size());

            for (uint32_t rm_id : ids) {
                const auto& rm = renderer.scene_manager.get_render_mesh(rm_id);
                DrawInstanceGPU di{};
                di.model = renderer.scene_manager.transforms().get_world_matrix(
                    rm.transform_index);
                di.meta = glm::uvec4(rm.material_index, 0u, 0u, 0u);
                renderer.draw_instances_cpu.push_back(di);
            }
            renderer.draw_batches.push_back(batch);
        }

        const VkDeviceSize bytes = std::max<size_t>(
            renderer.draw_instances_cpu.size() * sizeof(DrawInstanceGPU),
            sizeof(DrawInstanceGPU));
        if (!gfx::BufferUtils::initialize_buffer(
                renderer.vk.device.device, renderer.allocator, bytes,
                renderer.draw_instance_buffer)) {
            LOG_ERROR("[Draw] Failed to create draw_instance_buffer");
            return false;
        }
        if (!renderer.draw_instances_cpu.empty() &&
            renderer.draw_instance_buffer.mapped_data) {
            memcpy(renderer.draw_instance_buffer.mapped_data,
                   renderer.draw_instances_cpu.data(),
                   renderer.draw_instances_cpu.size() * sizeof(DrawInstanceGPU));
        }

        LOG_INFO("[Draw] Instancing: " << n_rm << " renderMeshes / "
                 << renderer.scene_manager.game_object_count() << " gameObjects → "
                 << renderer.draw_batches.size() << " batches");
    }

    {
        LOG_INFO("[Scene] Loaded scene '" << scene_name << "':"
                 << " textures=" << renderer.texture_manager.get_uploaded_count()
                 << " materials=" << renderer.material_manager.get_material_count()
                 << " meshPrims=" << renderer.mesh_manager.get_primitive_count()
                 << " gameObjects=" << renderer.scene_manager.game_object_count()
                 << " renderMeshes=" << renderer.scene_manager.render_mesh_count()
                 << " transforms=" << renderer.scene_manager.transforms().count()
                 << " verts=" << renderer.mesh_manager.get_total_vertex_count()
                 << " indices=" << renderer.mesh_manager.get_total_index_count());
    }

    // Commit double-buffered managers + bind descriptors.
    renderer.scene_manager.toggle_buffers();
    renderer.material_manager.toggle_buffers();
    renderer.mesh_manager.toggle_buffers();

    for (auto& set : renderer.vk.bindless_descriptor_sets) {
        // Binding 1: compact DrawInstanceGPU[] for instanced draws (not full SceneInstance)
        gfx::BufferUtils::update_descriptor(
            renderer.vk.device.device, renderer.draw_instance_buffer, set,
            std::max<VkDeviceSize>(
                renderer.draw_instances_cpu.size() * sizeof(DrawInstanceGPU),
                sizeof(DrawInstanceGPU)),
            1);
        renderer.material_manager.bind_descriptor(2, set);
        renderer.mesh_manager.bind_descriptor(3, 4, 5, set);
        renderer.texture_manager.bind_descriptor(Renderer::BINDING_TEXTURES, set);
    }

    // glTF camera support: if the scene contains a camera node, use its
    // world transform + projection parameters for the initial view.
    // Falls back to full-scene AABB framing when no camera is present.
    {
        auto [center, radius] =
            renderer.scene_manager.get_scene_framing_sphere();
        renderer.scene_center = center;

        if (loaded_camera.valid) {
            camera.set_from_camera_node(loaded_camera.world_transform,
                                        loaded_camera.yfov,
                                        loaded_camera.znear,
                                        loaded_camera.zfar,
                                        loaded_camera.aspectRatio);
            LOG_INFO("[Camera] Initial view from glTF camera node index "
                     << loaded_camera.gltf_camera_index);
        } else {
            camera.frame(center, radius);
            LOG_INFO("[Camera] Initial view from scene AABB frame: center=("
                     << center.x << ", " << center.y << ", " << center.z
                     << ") radius=" << radius);
        }
    }

    {
        const glm::vec3 pos = camera.get_position();
        const glm::vec3 fwd = camera.get_forward();
        LOG_INFO("[Camera] pose after load: pos=(" << pos.x << ", " << pos.y
                 << ", " << pos.z << ") forward=(" << fwd.x << ", " << fwd.y
                 << ", " << fwd.z << ")");
    }

    // Log scene lights (after world transforms) for import / exposure debugging.
    if (!renderer.lights.empty()) {
        LOG_INFO("[Lights] scene lights=" << renderer.lights.size()
                 << " scene_center=(" << renderer.scene_center.x << ", "
                 << renderer.scene_center.y << ", " << renderer.scene_center.z
                 << ")");
        for (size_t i = 0; i < renderer.lights.size(); ++i) {
            const auto& L = renderer.lights[i];
            LOG_INFO("[Lights]   [" << i << "] type=" << static_cast<uint32_t>(L.type)
                     << " pos/dir=(" << L.positionOrDirection.x << ", "
                     << L.positionOrDirection.y << ", " << L.positionOrDirection.z
                     << ") intensity=" << L.intensity << " range=" << L.range
                     << " contrib~"
                     << gfx::estimate_light_contribution(L, renderer.scene_center));
        }
        LOG_INFO("[Lights] auto exposure="
                 << gfx::compute_auto_exposure(renderer.lights, renderer.scene_center));
    } else {
        LOG_INFO("[Lights] no KHR_lights_punctual — using engine global directional");
    }

    camera.reset_mouse_state();

    // Seed both frame slots with lighting, then bind descriptors.
    for (uint32_t i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; ++i) {
        write_frame_lighting(i);
    }
    bind_frame_lighting_to_all_sets();

    return true;
}

void gfx::Engine::write_frame_lighting(uint32_t frame_index) {
    if (frame_index >= Renderer::MAX_FRAMES_IN_FLIGHT)
        return;

    auto* constants = static_cast<gfx::FrameConstants*>(
        renderer.frame_constants_buffer[frame_index].mapped_data);
    auto* gpu_lights = static_cast<gfx::GpuLight*>(
        renderer.frame_lights_buffer[frame_index].mapped_data);
    if (!constants || !gpu_lights)
        return;

    memset(constants, 0, sizeof(gfx::FrameConstants));
    memset(gpu_lights, 0, sizeof(gfx::GpuLight) * gfx::MAX_LIGHTS);

    // Diffuse IBL: SH from procedural environment (or modest fallback).
    if (renderer.ibl.ready) {
        for (int i = 0; i < 9; ++i)
            constants->shCoefficients[i] = renderer.ibl.sh_coefficients[i];
        // Indices reserved for bindless path; cube/LUT use dedicated bindings 7/8.
        constants->iblIndices = glm::uvec4(1u, 1u, 0u, 0u); // non-zero = specular IBL enabled
    } else {
        constants->shCoefficients[0] = glm::vec4(0.03f, 0.032f, 0.038f, 0.0f);
        constants->iblIndices = glm::uvec4(0u, 0u, 0u, 0u);
    }

    std::vector<gfx::Light> active;
    active.reserve(gfx::MAX_LIGHTS);
    if (!renderer.lights.empty()) {
        for (size_t i = 0; i < renderer.lights.size() && active.size() < gfx::MAX_LIGHTS; ++i) {
            active.push_back(renderer.lights[i]);
        }
    } else {
        active.push_back(renderer.globalLight);
    }

    const uint32_t n = static_cast<uint32_t>(active.size());
    constants->lightMeta = glm::uvec4(n, 0u, 0u, 0u);

    for (uint32_t i = 0; i < n; ++i) {
        gpu_lights[i] = gfx::to_gpu_light(active[i]);
    }

    const float exposure =
        gfx::compute_auto_exposure(active, renderer.scene_center);
    constants->cameraPosition = glm::vec4(camera.get_position(), exposure);

    // Descriptors may already point at these buffers; update ranges for safety.
    gfx::BufferUtils::update_descriptor(
        renderer.vk.device.device,
        renderer.frame_constants_buffer[frame_index],
        renderer.vk.bindless_descriptor_sets[frame_index],
        sizeof(gfx::FrameConstants),
        Renderer::BINDING_FRAME_CONSTANTS,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    gfx::BufferUtils::update_descriptor(
        renderer.vk.device.device,
        renderer.frame_lights_buffer[frame_index],
        renderer.vk.bindless_descriptor_sets[frame_index],
        static_cast<VkDeviceSize>(gfx::MAX_LIGHTS) * sizeof(gfx::GpuLight),
        Renderer::BINDING_LIGHTS,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
}

void gfx::Engine::bind_frame_lighting_to_all_sets() {
    for (uint32_t i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; ++i) {
        gfx::BufferUtils::update_descriptor(
            renderer.vk.device.device,
            renderer.frame_constants_buffer[i],
            renderer.vk.bindless_descriptor_sets[i],
            sizeof(gfx::FrameConstants),
            Renderer::BINDING_FRAME_CONSTANTS,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        gfx::BufferUtils::update_descriptor(
            renderer.vk.device.device,
            renderer.frame_lights_buffer[i],
            renderer.vk.bindless_descriptor_sets[i],
            static_cast<VkDeviceSize>(gfx::MAX_LIGHTS) * sizeof(gfx::GpuLight),
            Renderer::BINDING_LIGHTS,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        if (renderer.ibl.ready) {
            renderer.ibl.bind_descriptors(renderer.vk.device.device,
                                          renderer.vk.bindless_descriptor_sets[i],
                                          Renderer::BINDING_IBL_SPECULAR,
                                          Renderer::BINDING_IBL_BRDF_LUT);
        }
    }
}

void gfx::Engine::cleanup_scene() {
    renderer.lights.clear();
    renderer.draw_batches.clear();
    renderer.draw_instances_cpu.clear();
    gfx::BufferUtils::destroy_buffer(renderer.vk.device, renderer.allocator,
                                     renderer.draw_instance_buffer);
    renderer.scene_manager.clear_scene_data();
    renderer.scene_manager.shutdown();
}