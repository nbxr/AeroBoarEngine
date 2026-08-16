#include "gfx/Engine.h"
#include "gfx/BufferUtils.h"
#include "gfx/Renderer.h"
#include "gfx/TextureManager.h"
#include "gfx/DrawBatch.h"
#include "core/Configuration.h"
#include "core/Frustum.h"
#include "core/Log.h"
#include <vulkan/vulkan.h>
#include "scene/GltfLoader.h"
#include "scene/SceneManager.h"
#include "ecs/GltfEcsLoader.h"
#include "ecs/DesktopMoveSystem.h"
#include "ecs/LocomotionAnimSystem.h"
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

    // Fresh ECS world for this scene (scripts unbound on clear).
    ecs_world.clear();

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
        def.emissive_factor = glm::vec4(0.f, 0.f, 0.f, 1.f);
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
    // direction is the *to-light* vector (shader L for NdotL).
    if (renderer.globalLight.type == gfx::LightType::Directional &&
        glm::length(renderer.globalLight.direction) < 0.001f) {
        renderer.globalLight = {};
        renderer.globalLight.type = gfx::LightType::Directional;
        renderer.globalLight.direction = glm::normalize(glm::vec3(0.35f, 1.0f, 0.25f));
        renderer.globalLight.color = glm::vec3(1.0f, 0.98f, 0.95f);
        renderer.globalLight.intensity = 3.0f;
        renderer.globalLight.enabled = true;
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

    // Build TransformManager hierarchy (local + parent), then propagate() once.
    // Persist gltf_node → transform for animation channel targeting.
    auto& xforms = renderer.scene_manager.transforms();
    auto& node_to_xform = renderer.scene_manager.gltf_node_to_transform();
    node_to_xform.assign(model.nodes.size(), scene::TransformManager::kInvalid);

    // Light/camera nodes capture transform indices; world applied after propagate.
    std::vector<int> light_node_for_light(renderer.lights.size(), -1);
    int camera_node_index = -1;

    std::function<void(int, uint32_t)> add_mesh_node =
        [&](int node_idx, uint32_t parent_xform) {
            if (node_idx < 0 || node_idx >= (int)model.nodes.size())
                return;
            const auto &node = model.nodes[node_idx];
            const scene::LocalTrs local_trs = scene::GltfLoader::extract_node_trs(node);

            const uint32_t xform = xforms.allocate();
            xforms.set_local_trs(xform, local_trs);
            xforms.set_parent(xform, parent_xform);
            node_to_xform[static_cast<size_t>(node_idx)] = xform;

            if (node.light >= 0 && node.light < (int)light_node_for_light.size()) {
                light_node_for_light[static_cast<size_t>(node.light)] = node_idx;
            }

            if (!loaded_camera.valid && node.camera >= 0 &&
                node.camera < (int)model.cameras.size()) {
                const auto &cam = model.cameras[node.camera];
                if (cam.type == "perspective") {
                    const auto &p = cam.perspective;
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
                    camera_node_index = node_idx;
                }
            }

            if (node.mesh >= 0 && node.mesh < (int)model.meshes.size()) {
                const size_t mesh_idx = static_cast<size_t>(node.mesh);
                size_t prim_i = 0;
                size_t mat_offset = prim_material_offsets[mesh_idx];

                // glTF node.skin index maps to SkinSystem order (same as model.skins).
                uint32_t skin_index = ~0u;
                if (node.skin >= 0)
                    skin_index = static_cast<uint32_t>(node.skin);

                // One GameObject per mesh node; share the node's transform.
                const uint32_t go_id = renderer.scene_manager.create_game_object(
                    xform, static_cast<uint32_t>(node_idx), skin_index);

                // Mesh node transform for inv(meshWorld) * joint * IBM skinning.
                // SkinSystem is filled after the hierarchy walk; stash on GO for now
                // via skin_index + xform pairing applied after skins load.

                for (const auto &prim : model.meshes[mesh_idx].primitives) {
                    if (prim_i + mat_offset >= mesh_lookup.size())
                        break;

                    const uint32_t mesh_prim_id =
                        mesh_lookup[prim_i + mat_offset];
                    int mat_idx = prim.material;
                    // KHR_materials_variants: use mapping for variant 0 (first named).
                    {
                        auto vit = prim.extensions.find("KHR_materials_variants");
                        if (vit != prim.extensions.end() && vit->second.IsObject()) {
                            const auto& vobj = vit->second.Get<tinygltf::Value::Object>();
                            auto mit = vobj.find("mappings");
                            if (mit != vobj.end() && mit->second.IsArray()) {
                                const auto& maps = mit->second.Get<tinygltf::Value::Array>();
                                for (const auto& entry : maps) {
                                    if (!entry.IsObject())
                                        continue;
                                    const auto& eobj = entry.Get<tinygltf::Value::Object>();
                                    auto vars = eobj.find("variants");
                                    auto mmat = eobj.find("material");
                                    if (vars == eobj.end() || mmat == eobj.end() ||
                                        !vars->second.IsArray() || !mmat->second.IsNumber())
                                        continue;
                                    bool has_v0 = false;
                                    for (const auto& v : vars->second.Get<tinygltf::Value::Array>()) {
                                        if (v.IsNumber() && v.GetNumberAsInt() == 0) {
                                            has_v0 = true;
                                            break;
                                        }
                                    }
                                    if (has_v0) {
                                        mat_idx = mmat->second.GetNumberAsInt();
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (mat_idx < 0 || mat_idx >= (int)material_lookup.size()) {
                        mat_idx = 0;
                    }
                    const uint32_t material_id = material_lookup[mat_idx];
                    core::AABB local_aabb =
                        renderer.mesh_manager.get_primitive_local_aabb(
                            mesh_prim_id);

                    renderer.scene_manager.add_render_mesh(
                        go_id, mesh_prim_id, material_id, local_aabb, xform);
                    prim_i++;
                }
            }

            for (int child : node.children) {
                add_mesh_node(child, xform);
            }
        };

    int active_scene = (model.defaultScene >= 0) ? model.defaultScene : 0;
    if (!model.scenes.empty() && active_scene < (int)model.scenes.size()) {
        for (int root_node : model.scenes[active_scene].nodes) {
            add_mesh_node(root_node, scene::TransformManager::kInvalid);
        }
    } else {
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (model.nodes[i].mesh >= 0) {
                add_mesh_node(static_cast<int>(i), scene::TransformManager::kInvalid);
            }
        }
    }

    // Compose world = parent_world * local for every node (load-time full dirty).
    xforms.mark_all_dirty();
    xforms.propagate();
    LOG_INFO("[Transform] Propagated hierarchy (" << xforms.count()
             << " transform slots)");

    // worldScale: uniform load-time scale of scene roots (visual + physics).
    // Tabletop assets (chess, dice) are often cm–dm thick while Jolt defaults
    // (e.g. ~2 cm penetration slop) are tuned for larger props — scale them up
    // in sim space, keep gravity 9.81. Omit or 1.0 = identity. Mass × S³ in
    // spawn_scene_physics. Legacy key: debugWorldScale.
    float world_scale = 1.0f;
    {
        if (root.contains("worldScale") && root["worldScale"].is_number())
            world_scale = static_cast<float>(root["worldScale"].get<double>());
        else if (root.contains("debugWorldScale") &&
                 root["debugWorldScale"].is_number())
            world_scale =
                static_cast<float>(root["debugWorldScale"].get<double>());
        if (world_scale < 1e-6f)
            world_scale = 1.0f;
    }
    if (std::abs(world_scale - 1.0f) > 1e-5f) {
        uint32_t n_roots = 0;
        for (uint32_t i = 0; i < xforms.count(); ++i) {
            if (!xforms.is_alive(i))
                continue;
            if (xforms.get_parent(i) != scene::TransformManager::kInvalid)
                continue;
            scene::LocalTrs trs = xforms.get_local_trs(i);
            trs.translation *= world_scale;
            trs.scale *= world_scale;
            xforms.set_local_trs(i, trs);
            ++n_roots;
        }
        xforms.mark_all_dirty();
        xforms.propagate();
        LOG_INFO("[Scene] worldScale=" << world_scale << " applied to "
                 << n_roots << " root transform(s) (mesh + physics)");
    }

    // Morph targets (blend shapes) then skins then animations.
    {
        std::vector<uint32_t> mesh_ids;
        mesh_ids.reserve(mesh_lookup.size());
        for (const auto& id : mesh_lookup)
            mesh_ids.push_back(static_cast<uint32_t>(id));

        auto& morphs = renderer.scene_manager.morphs();
        morphs.load_from_gltf(model, mesh_ids);
        morphs.bind_nodes(model);
    }

    // Skins (joint maps + IBM) then animations (node TRS + morph weights).
    {
        auto& skins = renderer.scene_manager.skins();
        skins.destroy(renderer.vk.device.device, renderer.allocator);
        const uint32_t nskins = skins.load_from_gltf(model, node_to_xform);
        if (nskins > 0) {
            // Bind mesh node world to each skin (for inv(meshWorld) formula).
            const uint32_t n_go = renderer.scene_manager.game_object_count();
            for (uint32_t gi = 0; gi < n_go; ++gi) {
                const auto& go = renderer.scene_manager.get_game_object(gi);
                if (go.skin_index != ~0u)
                    skins.set_mesh_transform(go.skin_index, go.root_transform_index);
            }
            if (!skins.create_gpu_buffers(renderer.vk.device.device,
                                          renderer.allocator)) {
                LOG_ERROR("[Skin] GPU joint buffers failed");
            }
        }

        auto& anims = renderer.scene_manager.animations();
        const uint32_t nclips = anims.load_from_gltf(
            model, node_to_xform, &renderer.scene_manager.morphs());
        if (nclips > 0) {
            // Exclusive default (Walk preferred) — multi-clip assets like Fox
            // must not play all channels at once.
            anims.play_default_clip(/*loop=*/true);
            if (nclips > 1) {
                LOG_INFO("[Anim] " << nclips
                         << " clips loaded — press N to cycle (exclusive play)");
            }
        } else {
            LOG_INFO("[Anim] No node animations in this scene");
        }
    }

    // Apply world transforms to lights / camera from the hierarchy.
    // Store TransformManager index so runtime can re-sync dynamic light nodes.
    for (size_t i = 0; i < renderer.lights.size(); ++i) {
        const int nidx = light_node_for_light[i];
        if (nidx < 0 || static_cast<size_t>(nidx) >= node_to_xform.size())
            continue;
        const uint32_t xi = node_to_xform[static_cast<size_t>(nidx)];
        if (xi == scene::TransformManager::kInvalid)
            continue;
        renderer.lights[i].transform_index = xi;
        scene::GltfLoader::apply_world_transform_to_light(
            renderer.lights[i], xforms.get_world_matrix(xi));
    }
    if (loaded_camera.valid && camera_node_index >= 0 &&
        static_cast<size_t>(camera_node_index) < node_to_xform.size()) {
        const uint32_t xi =
            node_to_xform[static_cast<size_t>(camera_node_index)];
        if (xi != scene::TransformManager::kInvalid)
            loaded_camera.world_transform = xforms.get_world_matrix(xi);
    }

    // Refresh dual-written SceneInstance worlds (were written before propagate).
    renderer.scene_manager.refresh_instance_worlds();

    // Upload CPU data (populated by GltfLoader) into the persistently-mapped
    // GPU buffers on the "upload" side of each double-buffered manager.
    renderer.scene_manager.update_buffers();
    renderer.material_manager.update_buffers();
    renderer.mesh_manager.update_buffers();
    renderer.texture_manager.upload_textures();

    // Mesh templates + GPU cull resources (fixed per-batch instance regions).
    // (Descriptors for instances rebinding happens after toggle below as well.)
    if (!rebuild_draw_batches())
        return false;

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

    for (uint32_t i = 0; i < renderer.vk.bindless_descriptor_sets.size(); ++i) {
        auto& set = renderer.vk.bindless_descriptor_sets[i];
        // Binding 1: GPU-written instance buffer for this frame slot
        if (renderer.gpu_culling.is_ready()) {
            auto& inst = renderer.gpu_culling.out_instances(i);
            gfx::BufferUtils::update_descriptor(
                renderer.vk.device.device, inst, set, inst.info.size,
                Renderer::BINDING_DRAW_INSTANCES);
        }
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

    // ECS Phase 3: dual-write entities + extras.ECS_Components_v1; default player
    // if none authored. Player owns view (first PlayerTag wins).
    {
        ecs::populate_world_from_gltf(ecs_world, model, renderer.scene_manager);
        // Authored eye_offset / boom_offset are in asset meters; match worldScale.
        if (std::abs(world_scale - 1.0f) > 1e-5f) {
            for (ecs::CameraRig& rig : ecs_world.camera_rigs.data()) {
                rig.eye_offset *= world_scale;
                rig.boom_offset *= world_scale;
            }
        }
        if (ecs_world.player_tags.size() == 0) {
            const ecs::Entity player = ecs_world.spawn_default_desktop_player();
            ecs::sync_rig_from_camera(ecs_world, player, camera);
            if (std::abs(world_scale - 1.0f) > 1e-5f) {
                if (ecs::CameraRig* rig = ecs_world.camera_rigs.try_get(player)) {
                    rig->movement_speed *= world_scale;
                    rig->near_plane *= world_scale;
                    rig->far_plane *= world_scale;
                    rig->eye_offset *= world_scale;
                }
            }
            ecs::sync_camera_from_rig(ecs_world, player, camera);
        } else {
            ecs_world.resolve_active_player();
            const ecs::Entity player = ecs_world.active_player();
            // Tunables from framed camera; eye_offset stays authoring-owned
            // (already scaled once above).
            ecs::sync_rig_from_camera(ecs_world, player, camera);
            if (std::abs(world_scale - 1.0f) > 1e-5f) {
                if (ecs::CameraRig* rig = ecs_world.camera_rigs.try_get(player)) {
                    rig->movement_speed *= world_scale;
                    rig->near_plane *= world_scale;
                    rig->far_plane *= world_scale;
                }
            }
            ecs::sync_camera_from_rig(ecs_world, player, camera);
            // FPS: eye at root + eye_offset. Third-person: orbit boom + look-at.
            ecs::place_camera_on_player(ecs_world, player, camera,
                                        renderer.scene_manager.transforms());
            {
                const glm::vec3 pos = camera.get_position();
                const ecs::CameraRig* rig = ecs_world.camera_rigs.try_get(player);
                const glm::vec3 eye =
                    rig ? rig->eye_offset : glm::vec3(0.f, 0.08f, 0.f);
                const glm::vec3 boom =
                    rig ? rig->boom_offset : glm::vec3(0.f);
                const bool tp = rig && rig->third_person;
                LOG_INFO("[ECS] Player camera at "
                         << (tp ? "third_person" : "first_person") << " pos=("
                         << pos.x << ", " << pos.y << ", " << pos.z
                         << ") eye_offset=(" << eye.x << ", " << eye.y << ", "
                         << eye.z << ") boom=(" << boom.x << ", " << boom.y
                         << ", " << boom.z << ")");
            }
        }
        if (std::abs(world_scale - 1.0f) > 1e-5f) {
            for (ecs::LocomotionAnim& loco : ecs_world.locomotion_anims.data()) {
                if (loco.walk_threshold < 1.0e8f)
                    loco.walk_threshold *= world_scale;
                if (loco.run_threshold < 1.0e8f)
                    loco.run_threshold *= world_scale;
            }
        }
        ecs::bind_player_animation_masks(ecs_world, renderer.scene_manager,
                                         model);
        ecs::locomotion_anim_bind_clips(ecs_world,
                                        renderer.scene_manager.animations());
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
                     << " pos=(" << L.position.x << ", " << L.position.y << ", "
                     << L.position.z << ") dir=(" << L.direction.x << ", "
                     << L.direction.y << ", " << L.direction.z
                     << ") intensity=" << L.intensity << " range=" << L.range
                     << " cone=[" << L.innerConeAngle << "," << L.outerConeAngle
                     << "] xform=" << L.transform_index << " contrib~"
                     << gfx::estimate_light_contribution(L, renderer.scene_center));
        }
        LOG_INFO("[Lights] auto exposure="
                 << gfx::compute_auto_exposure(renderer.lights, renderer.scene_center));
    } else {
        LOG_INFO("[Lights] no KHR_lights_punctual — using engine global directional");
    }

    camera.reset_mouse_state();

    // Scene physics from KHR_physics_rigid_bodies (pieces as single hulls; pawn
    // tops are child meshes without their own body — they follow the body node).
    {
        bool enable_scene_phys = true;
        const nlohmann::json& cfg = core::Configuration::get_root();
        if (cfg.contains("scenePhysics") && cfg["scenePhysics"].is_boolean())
            enable_scene_phys = cfg["scenePhysics"].get<bool>();
        if (enable_scene_phys) {
            if (!spawn_scene_physics(model)) {
                LOG_ERROR("[Physics] spawn_scene_physics failed "
                          "(continuing without colliders)");
            }
            configure_kill_floor();
        } else {
            kill_floor_enabled_ = false;
        }
    }

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

    // Diffuse IBL: SH from procedural / HDR environment (or modest fallback).
    if (renderer.ibl.ready) {
        for (int i = 0; i < 9; ++i)
            constants->shCoefficients[i] = renderer.ibl.sh_coefficients[i];
        // Indices reserved for bindless path; cube/LUT use dedicated bindings 7/8.
        constants->iblIndices = glm::uvec4(1u, 1u, 0u, 0u); // non-zero = specular IBL enabled
    } else {
        constants->shCoefficients[0] = glm::vec4(0.03f, 0.032f, 0.038f, 0.0f);
        constants->iblIndices = glm::uvec4(0u, 0u, 0u, 0u);
    }

    // Active lights: scene list (enabled only), else global directional.
    // Rebuilt every frame so CPU-side mutation (dynamic lights) is free.
    std::vector<gfx::Light> active;
    active.reserve(gfx::MAX_LIGHTS);
    if (!renderer.lights.empty()) {
        for (size_t i = 0; i < renderer.lights.size() && active.size() < gfx::MAX_LIGHTS;
             ++i) {
            if (renderer.lights[i].enabled)
                active.push_back(renderer.lights[i]);
        }
    }
    if (active.empty() && renderer.globalLight.enabled) {
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

void gfx::Engine::refresh_lights_from_transforms() {
    auto& xforms = renderer.scene_manager.transforms();
    for (auto& L : renderer.lights) {
        if (L.transform_index == gfx::kInvalidLightTransform)
            continue;
        if (L.transform_index >= xforms.count())
            continue;
        scene::GltfLoader::apply_world_transform_to_light(
            L, xforms.get_world_matrix(L.transform_index));
    }
}

bool gfx::Engine::sync_scene_transforms() {
    // CPU hierarchy: propagate dirty locals → worlds + dual-write SceneInstance.
    const bool worlds_changed = renderer.scene_manager.sync_transforms();
    if (worlds_changed) {
        refresh_lights_from_transforms();
        // Both frame slots must see new models; each uploads after its own fence wait.
        renderer.transform_upload_mask = (1u << Renderer::MAX_FRAMES_IN_FLIGHT) - 1u;
    }

    // Joint palettes always refresh when anything is dirty or any skin is playing.
    // Use transform_upload_mask so each FIF slot gets a coherent palette write.
    const uint32_t bit = 1u << renderer.current_frame;
    const bool need_upload = (renderer.transform_upload_mask & bit) != 0 ||
                             worlds_changed ||
                             renderer.scene_manager.skins().has_skins();

    if (need_upload && renderer.scene_manager.skins().has_skins()) {
        renderer.scene_manager.skins().update_joint_matrices(
            renderer.current_frame, renderer.scene_manager.transforms());
        // Bind this frame's joint buffer (host-visible, updated above).
        auto& jb = renderer.scene_manager.skins().joint_buffer(renderer.current_frame);
        gfx::BufferUtils::update_descriptor(
            renderer.vk.device.device, jb,
            renderer.vk.bindless_descriptor_sets[renderer.current_frame],
            renderer.scene_manager.skins().joint_buffer_size(),
            Renderer::BINDING_JOINT_MATRICES);
    }

    if ((renderer.transform_upload_mask & bit) == 0 && !worlds_changed)
        return worlds_changed;

    if (renderer.gpu_culling.is_ready() &&
        (renderer.transform_upload_mask & bit) != 0) {
        renderer.gpu_culling.update_models(renderer.current_frame,
                                           renderer.scene_manager);
    }
    renderer.transform_upload_mask &= ~bit;
    return true;
}

void gfx::Engine::update_animations(float delta_time) {
    auto& sm = renderer.scene_manager;
    sm.animations().update(delta_time, sm.transforms(), &sm.morphs());
    // CPU morph: blend deltas into MeshManager vertex buffers (both FIF sides).
    if (sm.morphs().has_morphs())
        sm.morphs().apply(renderer.mesh_manager);
}

bool gfx::Engine::set_light(uint32_t index, const gfx::Light& light) {
    if (index >= renderer.lights.size())
        return false;
    // Preserve transform link unless caller set a new one.
    const uint32_t prev_xform = renderer.lights[index].transform_index;
    renderer.lights[index] = light;
    if (light.transform_index == gfx::kInvalidLightTransform)
        renderer.lights[index].transform_index = prev_xform;
    return true;
}

uint32_t gfx::Engine::add_light(const gfx::Light& light) {
    if (renderer.lights.size() >= gfx::MAX_LIGHTS) {
        LOG_ERROR("[Lights] add_light failed: already at MAX_LIGHTS ("
                  << gfx::MAX_LIGHTS << ")");
        return gfx::MAX_LIGHTS; // invalid
    }
    renderer.lights.push_back(light);
    return static_cast<uint32_t>(renderer.lights.size() - 1);
}

bool gfx::Engine::set_light_enabled(uint32_t index, bool enabled) {
    if (index >= renderer.lights.size())
        return false;
    renderer.lights[index].enabled = enabled;
    return true;
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
        if (renderer.scene_manager.skins().has_skins()) {
            auto& jb = renderer.scene_manager.skins().joint_buffer(i);
            gfx::BufferUtils::update_descriptor(
                renderer.vk.device.device, jb,
                renderer.vk.bindless_descriptor_sets[i],
                renderer.scene_manager.skins().joint_buffer_size(),
                Renderer::BINDING_JOINT_MATRICES);
        }
    }
}

void gfx::Engine::cleanup_scene() {
    if (renderer.vk.device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(renderer.vk.device.device);
    }

    // Drop rigid bodies so transform links are not dangling after clear.
    if (physics.is_initialized())
        physics.shutdown();

    ecs_world.clear();

    renderer.lights.clear();
    renderer.mesh_draw_infos.clear();
    renderer.gpu_culling.clear_scene(renderer.vk.device, renderer.allocator);
    renderer.last_visible_instances = 0;
    renderer.last_total_render_meshes = 0;
    renderer.scene_manager.clear_scene_data();
}