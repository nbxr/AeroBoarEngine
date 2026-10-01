#include "gfx/Engine.h"

#include "gfx/BufferUtils.h"
#include "gfx/MeshData.h"
#include "scene/NativeScene.h"
#include "ecs/DesktopMoveSystem.h"
#include "ecs/LocomotionAnimSystem.h"
#include "ecs/ScriptSystem.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace {

int ms_between(std::chrono::steady_clock::time_point a,
               std::chrono::steady_clock::time_point b) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count());
}

uint32_t native_flags(bool optimize_meshes, bool scene_physics) {
    uint32_t flags = 0;
    if (optimize_meshes)
        flags |= scene::kNativeFlagOptimizeMeshes;
    if (scene_physics)
        flags |= scene::kNativeFlagScenePhysics;
    return flags;
}

bool parents_acyclic(const std::vector<scene::NativeXform>& xforms) {
    const uint32_t n = static_cast<uint32_t>(xforms.size());
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t p = xforms[i].parent;
        uint32_t guard = 0;
        while (p != scene::TransformManager::kInvalid) {
            if (p >= n || ++guard > n)
                return false;
            p = xforms[p].parent;
        }
    }
    return true;
}

template <typename T>
bool copy_pairs(const ecs::ComponentStore<T>& store, uint32_t entity_count,
                std::vector<std::pair<ecs::Entity, T>>& out) {
    const auto& ents = store.entities();
    const auto& data = store.data();
    if (ents.size() != data.size())
        return false;
    out.clear();
    out.reserve(ents.size());
    for (size_t i = 0; i < ents.size(); ++i) {
        if (ents[i] >= entity_count)
            return false;
        out.emplace_back(ents[i], data[i]);
    }
    return true;
}

template <typename T>
bool copy_ids(const ecs::ComponentStore<T>& store, uint32_t entity_count,
              std::vector<ecs::Entity>& out) {
    out.clear();
    for (ecs::Entity e : store.entities()) {
        if (e >= entity_count)
            return false;
        out.push_back(e);
    }
    return true;
}

} // namespace

bool gfx::Engine::apply_native_cpu(scene::NativeSceneFile& file) {
    if (renderer.material_manager.get_material_count() != 0 ||
        renderer.mesh_manager.cpu_mesh_count() != 0 ||
        renderer.scene_manager.game_object_count() != 0 ||
        renderer.texture_manager.get_uploaded_count() != 0) {
        LOG_ERROR("[Native] managers are not empty; refusing to apply cache");
        return false;
    }

    for (size_t i = 0; i < file.images.size(); ++i) {
        const gfx::CachedImage& img = file.images[i];
        const uint64_t expect = static_cast<uint64_t>(img.width) *
                                static_cast<uint64_t>(img.height) * 4ull;
        if (img.name.empty() || img.rgba.size() != expect) {
            LOG_ERROR("[Native] texture " << i << " pixels do not match its size");
            return false;
        }
        const gfx::TextureID id = renderer.texture_manager.get_texture_handle_from_pixels(
            img.name, img.rgba.data(), static_cast<int>(img.width),
            static_cast<int>(img.height), 4, img.srgb != 0);
        if (static_cast<uint32_t>(id) != static_cast<uint32_t>(i)) {
            LOG_ERROR("[Native] texture '" << img.name << "' did not land at index "
                                            << i);
            return false;
        }
    }

    for (size_t i = 0; i < file.materials.size(); ++i) {
        const gfx::MaterialID id =
            renderer.material_manager.create_material(file.materials[i]);
        if (static_cast<uint32_t>(id) != static_cast<uint32_t>(i)) {
            LOG_ERROR("[Native] material did not land at index " << i);
            return false;
        }
    }

    for (size_t i = 0; i < file.meshes.size(); ++i) {
        scene::NativeMesh& src = file.meshes[i];
        gfx::MeshData mesh;
        mesh.vertices = std::move(src.vertices);
        mesh.indices = std::move(src.indices);
        mesh.meshlets = std::move(src.meshlets);
        mesh.local_aabb = src.local_aabb;
        mesh.index16 = src.index16;
        mesh.allow_meshlet_cull = src.allow_meshlet_cull;
        const gfx::MeshPrimitiveID id = renderer.mesh_manager.add_mesh(mesh);
        if (static_cast<uint32_t>(id) != static_cast<uint32_t>(i)) {
            LOG_ERROR("[Native] mesh did not land at index " << i);
            return false;
        }
    }

    if (!parents_acyclic(file.xforms)) {
        LOG_ERROR("[Native] transform parents are cyclic or out of range");
        return false;
    }
    auto& xforms = renderer.scene_manager.transforms();
    const uint32_t nx = static_cast<uint32_t>(file.xforms.size());
    for (uint32_t i = 0; i < nx; ++i) {
        const uint32_t id = xforms.allocate();
        if (id != i) {
            LOG_ERROR("[Native] transform allocate returned " << id << " at " << i);
            return false;
        }
    }
    for (uint32_t i = 0; i < nx; ++i)
        xforms.set_local_trs(i, file.xforms[i].trs);
    for (uint32_t i = 0; i < nx; ++i) {
        const uint32_t parent = file.xforms[i].parent;
        xforms.set_parent(i, parent);
        if (xforms.get_parent(i) != parent) {
            LOG_ERROR("[Native] set_parent failed at transform " << i);
            return false;
        }
    }
    if (nx > 0) {
        xforms.mark_all_dirty();
        xforms.propagate();
    }

    for (uint32_t xi : file.node_to_xform) {
        if (xi != scene::TransformManager::kInvalid && xi >= nx) {
            LOG_ERROR("[Native] node map points at missing transform " << xi);
            return false;
        }
    }
    renderer.scene_manager.gltf_node_to_transform() = file.node_to_xform;

    const uint32_t nmesh = static_cast<uint32_t>(file.meshes.size());
    const uint32_t nmat = static_cast<uint32_t>(file.materials.size());
    const uint32_t nskin = static_cast<uint32_t>(file.skins.size());
    const uint32_t ngo = static_cast<uint32_t>(file.game_objects.size());
    for (const scene::GameObject& go : file.game_objects) {
        if (go.root_transform_index >= nx ||
            (go.skin_index != ~0u && go.skin_index >= nskin)) {
            LOG_ERROR("[Native] game object '" << go.name << "' has a bad index");
            return false;
        }
    }
    for (const scene::RenderMesh& rm : file.render_meshes) {
        if (rm.game_object_index >= ngo || rm.mesh_index >= nmesh ||
            rm.material_index >= nmat || rm.transform_index >= nx) {
            LOG_ERROR("[Native] render mesh has a bad index");
            return false;
        }
    }

    auto& scene = renderer.scene_manager;
    for (size_t i = 0; i < file.game_objects.size(); ++i) {
        const scene::GameObject& go = file.game_objects[i];
        const uint32_t id = scene.create_game_object(
            go.root_transform_index, go.gltf_node_index, go.skin_index, go.name);
        if (id != static_cast<uint32_t>(i)) {
            LOG_ERROR("[Native] create_game_object failed at " << i);
            return false;
        }
    }
    for (size_t i = 0; i < file.render_meshes.size(); ++i) {
        const scene::RenderMesh& rm = file.render_meshes[i];
        const uint32_t id = scene.add_render_mesh(rm.game_object_index, rm.mesh_index,
                                                  rm.material_index, rm.local_aabb,
                                                  rm.transform_index);
        if (id != static_cast<uint32_t>(i)) {
            LOG_ERROR("[Native] add_render_mesh failed at " << i);
            return false;
        }
    }

    scene.morphs().install(std::move(file.morphs), std::move(file.node_to_morph));

    uint32_t expect_joints = 0;
    for (const scene::Skin& skin : file.skins) {
        if (skin.joint_transform_indices.empty())
            continue;
        if (expect_joints + skin.joint_transform_indices.size() > scene::kMaxJointsTotal) {
            LOG_ERROR("[Native] skins exceed kMaxJointsTotal");
            return false;
        }
        expect_joints += static_cast<uint32_t>(skin.joint_transform_indices.size());
    }
    auto& skins = scene.skins();
    skins.install(std::move(file.skins));
    if (skins.total_joints() != expect_joints) {
        LOG_ERROR("[Native] skin install dropped joints");
        return false;
    }
    if (skins.skin_count() > 0) {
        for (uint32_t gi = 0; gi < scene.game_object_count(); ++gi) {
            const scene::GameObject& go = scene.get_game_object(gi);
            if (go.skin_index != ~0u)
                skins.set_mesh_transform(go.skin_index, go.root_transform_index);
        }
        if (skins.total_joints() > 0 &&
            !skins.create_gpu_buffers(renderer.vk.device.device, renderer.allocator)) {
            LOG_ERROR("[Native] skin GPU buffers failed");
            return false;
        }
    }

    const bool any_clips = !file.clips.empty();
    scene.animations().install_clips(std::move(file.clips));
    if (any_clips)
        scene.animations().play_default_clip(true);

    renderer.lights = std::move(file.lights);
    renderer.globalLight = file.global_light;

    const scene::NativeEcs& ecs = file.ecs;
    ecs_world.restore_roster(ecs.entity_count, ecs.alive);
    if (ecs_world.entity_capacity() != ecs.entity_count) {
        LOG_ERROR("[Native] ECS roster restore failed");
        return false;
    }
    for (const auto& it : ecs.transform_links)
        ecs_world.transform_links.get_or_emplace(it.first, it.second);
    for (ecs::Entity e : ecs.player_tags)
        ecs_world.player_tags.get_or_emplace(e);
    for (ecs::Entity e : ecs.desktop_moves)
        ecs_world.desktop_moves.get_or_emplace(e);
    for (const auto& it : ecs.fps_moves)
        ecs_world.fps_moves.get_or_emplace(it.first, it.second);
    for (const auto& it : ecs.camera_rigs)
        ecs_world.camera_rigs.get_or_emplace(it.first, it.second);
    for (const auto& it : ecs.healths)
        ecs_world.healths.get_or_emplace(it.first, it.second);
    for (const auto& it : ecs.names)
        ecs_world.names.get_or_emplace(it.first, it.second);
    for (const auto& it : ecs.scripts) {
        ecs::Script sc;
        sc.name = it.second;
        ecs_world.scripts.get_or_emplace(it.first, sc);
    }
    for (const auto& it : ecs.locomotion)
        ecs_world.locomotion_anims.get_or_emplace(it.first, it.second);
    ecs_world.gltf_node_to_entity = ecs.gltf_node_to_entity;
    ecs_world.game_object_to_entity = ecs.game_object_to_entity;
    ecs_world.resolve_active_player();
    for (const auto& it : ecs.scripts) {
        if (!ecs_world.is_alive(it.first))
            continue;
        if (!ecs::script_system_bind(ecs_world, it.first))
            LOG_ERROR("[Native] script bind failed for '" << it.second << "'");
    }
    return true;
}

bool gfx::Engine::rollback_native_cpu() {
    ecs_world.clear();
    renderer.lights.clear();
    renderer.globalLight = {};
    renderer.scene_manager.skins().destroy(renderer.vk.device.device, renderer.allocator);
    renderer.scene_manager.clear_scene_data();
    renderer.mesh_manager.clear_all_caches();
    renderer.material_manager.clear_cpu();
    if (!renderer.texture_manager.clear_unuploaded()) {
        LOG_ERROR("[Native] rollback found textures that were already uploaded");
        return false;
    }
    return true;
}

bool gfx::Engine::upload_scene_resources(const std::string& scene_name) {
    renderer.scene_manager.refresh_instance_worlds();
    renderer.scene_manager.update_buffers();
    renderer.material_manager.update_buffers();
    renderer.mesh_manager.update_buffers();
    renderer.texture_manager.upload_textures();

    if (!rebuild_draw_batches())
        return false;

    last_scene_name_ = scene_name;
    LOG_VERBOSE("[Scene] Loaded scene '"
                << scene_name << "':"
                << " textures=" << renderer.texture_manager.get_uploaded_count()
                << " materials=" << renderer.material_manager.get_material_count()
                << " meshPrims=" << renderer.mesh_manager.get_primitive_count()
                << " gameObjects=" << renderer.scene_manager.game_object_count()
                << " renderMeshes=" << renderer.scene_manager.render_mesh_count()
                << " transforms=" << renderer.scene_manager.transforms().count()
                << " verts=" << renderer.mesh_manager.get_total_vertex_count()
                << " indices=" << renderer.mesh_manager.get_total_index_count());

    renderer.scene_manager.toggle_buffers();
    renderer.material_manager.toggle_buffers();
    renderer.mesh_manager.toggle_buffers();

    for (uint32_t i = 0; i < renderer.vk.bindless_descriptor_sets.size(); ++i) {
        auto& set = renderer.vk.bindless_descriptor_sets[i];
        if (renderer.gpu_culling.is_ready()) {
            auto& inst = renderer.gpu_culling.out_instances(i);
            gfx::BufferUtils::update_descriptor(renderer.vk.device.device, inst, set,
                                                inst.info.size,
                                                Renderer::BINDING_DRAW_INSTANCES);
        }
        renderer.material_manager.bind_descriptor(2, set);
        renderer.mesh_manager.bind_descriptor(3, 4, 5, set);
        renderer.texture_manager.bind_descriptor(Renderer::BINDING_TEXTURES, set);
    }
    return true;
}

void gfx::Engine::log_scene_load(const char* how, int scene_ms) {
    const auto& sm = renderer.scene_manager;
    const core::AABB box = sm.get_scene_aabb();
    // LOG_INFO declares its own stream named oss; a local with that name
    // makes LOG_INFO(oss.str()) print the empty macro stream.
    std::ostringstream line;
    line << std::fixed << std::setprecision(4);
    line << "[Scene] how=" << how << " ms=" << scene_ms
        << " tex=" << renderer.texture_manager.get_uploaded_count()
        << " mat=" << renderer.material_manager.get_material_count()
        << " mesh=" << renderer.mesh_manager.get_primitive_count()
        << " go=" << sm.game_object_count() << " rm=" << sm.render_mesh_count()
        << " xform=" << sm.transforms().count()
        << " vert=" << renderer.mesh_manager.get_total_vertex_count()
        << " idx=" << renderer.mesh_manager.get_total_index_count()
        << " bodies=" << physics.body_count()
        << " clips=" << sm.animations().clip_count()
        << " skins=" << sm.skins().skin_count()
        << " joints=" << sm.skins().total_joints();
    if (box.is_valid()) {
        line << " aabb=(" << box.min.x << "," << box.min.y << "," << box.min.z
             << ")-(" << box.max.x << "," << box.max.y << "," << box.max.z << ")";
    } else {
        line << " aabb=invalid";
    }
    if (physics.body_count() > 0) {
        glm::vec3 pos(0.0f);
        glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
        if (physics.get_pose(0, pos, rot)) {
            line << " body0=(" << pos.x << "," << pos.y << "," << pos.z << ")";
        }
    }
    LOG_INFO(line.str());
}

bool gfx::Engine::try_load_native_cache(const std::string& scene_name,
                                        const std::string& source_path,
                                        float world_scale, bool optimize_meshes,
                                        bool scene_physics, bool& gpu_dirty) {
    gpu_dirty = false;
    uint64_t source_size = 0;
    uint64_t source_mtime = 0;
    if (!scene::source_file_stamp(source_path, source_size, source_mtime)) {
        LOG_INFO("[Native] source stamp unavailable, loading glTF");
        return false;
    }
    const uint32_t flags = native_flags(optimize_meshes, scene_physics);
    const std::filesystem::path path = scene::native_cache_path(scene_name);
    const scene::NativeStamp stamp = scene::native_cache_stamp(
        path, scene_name, world_scale, flags, source_size, source_mtime);
    if (stamp != scene::NativeStamp::Match) {
        LOG_INFO("[Native] cache "
                 << (stamp == scene::NativeStamp::Missing ? "missing" : "mismatch")
                 << " " << path.string());
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    scene::NativeSceneFile file;
    if (!scene::read_native_scene(path, scene_name, world_scale, flags, source_size,
                                  source_mtime, file))
        return false;
    const auto t_read = std::chrono::steady_clock::now();

    if (!apply_native_cpu(file)) {
        if (!rollback_native_cpu())
            gpu_dirty = true;
        return false;
    }
    const auto t_cpu = std::chrono::steady_clock::now();

    if (!upload_scene_resources(scene_name)) {
        LOG_ERROR("[Native] GPU upload failed");
        gpu_dirty = true;
        return false;
    }
    const auto t_up = std::chrono::steady_clock::now();

    {
        auto [center, radius] = renderer.scene_manager.get_scene_framing_sphere();
        renderer.scene_center = center;
        const scene::NativeCamera& cam = file.camera;
        auto& xforms = renderer.scene_manager.transforms();
        if (cam.valid && cam.transform_index != scene::TransformManager::kInvalid &&
            xforms.is_alive(cam.transform_index)) {
            camera.set_from_camera_node(xforms.get_world_matrix(cam.transform_index),
                                        cam.yfov, cam.znear, cam.zfar, cam.aspect);
        } else {
            camera.frame(center, radius);
        }
        ecs_world.resolve_active_player();
        const ecs::Entity player = ecs_world.active_player();
        if (player != ecs::kInvalidEntity && ecs_world.is_alive(player)) {
            ecs::sync_camera_from_rig(ecs_world, player, camera);
            ecs::place_camera_on_player(ecs_world, player, camera, xforms);
        }
        ecs::bind_player_animation_masks(ecs_world, renderer.scene_manager,
                                         file.node_names);
        ecs::locomotion_anim_bind_clips(ecs_world, renderer.scene_manager.animations());
    }

    const auto t_phys0 = std::chrono::steady_clock::now();
    if (scene_physics) {
        if (!physics.is_initialized() && !physics.initialize()) {
            LOG_ERROR("[Native] physics initialize failed");
            gpu_dirty = true;
            return false;
        }
        if (!physics.import_cooked_bodies(file.bodies)) {
            LOG_ERROR("[Native] physics import failed");
            physics.shutdown();
            gpu_dirty = true;
            return false;
        }
        configure_kill_floor();
    } else {
        kill_floor_enabled_ = false;
    }
    const auto t_phys = std::chrono::steady_clock::now();

    mark_lights_dirty();
    for (uint32_t i = 0; i < Renderer::MAX_FRAMES_IN_FLIGHT; ++i)
        write_frame_lighting(i);
    bind_frame_lighting_to_all_sets();
    camera.reset_mouse_state();

    const auto t1 = std::chrono::steady_clock::now();
    LOG_INFO("[Native] load read_ms="
             << ms_between(t0, t_read) << " cpu_ms=" << ms_between(t_read, t_cpu)
             << " upload_ms=" << ms_between(t_cpu, t_up)
             << " physics_ms=" << ms_between(t_phys0, t_phys)
             << " total_ms=" << ms_between(t0, t1));
    log_scene_load("native", ms_between(t0, t1));
    return true;
}

bool gfx::Engine::store_native_cache(
    const std::string& scene_name, const std::string& source_path, float world_scale,
    bool optimize_meshes, bool scene_physics, const std::vector<std::string>& node_names,
    bool cam_valid, float yfov, float aspect, float znear, float zfar, int cam_gltf,
    uint32_t cam_xform, const std::vector<gfx::CachedImage>& images) {
    uint64_t source_size = 0;
    uint64_t source_mtime = 0;
    if (!scene::source_file_stamp(source_path, source_size, source_mtime)) {
        LOG_ERROR("[Native] cannot stamp source " << source_path);
        return false;
    }

    scene::NativeSceneFile file;
    file.images = images;
    std::unordered_set<std::string> seen_names;
    for (size_t i = 0; i < file.images.size(); ++i) {
        if (file.images[i].name.empty())
            file.images[i].name = "tex_" + std::to_string(i);
        if (!seen_names.insert(file.images[i].name).second) {
            LOG_ERROR("[Native] duplicate texture name '" << file.images[i].name
                                                          << "'");
            return false;
        }
    }

    const uint32_t nmat = renderer.material_manager.get_material_count();
    file.materials.reserve(nmat);
    for (uint32_t i = 0; i < nmat; ++i) {
        const gfx::Material* mat = renderer.material_manager.get_material(i);
        if (!mat) {
            LOG_ERROR("[Native] material slot " << i << " is empty");
            return false;
        }
        file.materials.push_back(*mat);
    }

    const uint32_t nmesh = renderer.mesh_manager.cpu_mesh_count();
    file.meshes.resize(nmesh);
    for (uint32_t i = 0; i < nmesh; ++i) {
        const gfx::MeshData* md = renderer.mesh_manager.cpu_mesh(i);
        if (!md) {
            LOG_ERROR("[Native] mesh slot " << i << " is empty");
            return false;
        }
        scene::NativeMesh& dst = file.meshes[i];
        dst.vertices = md->vertices;
        dst.indices = md->indices;
        dst.meshlets = md->meshlets;
        dst.local_aabb = md->local_aabb;
        dst.index16 = md->index16;
        dst.allow_meshlet_cull = md->allow_meshlet_cull;
    }

    const auto& xforms = renderer.scene_manager.transforms();
    const uint32_t nx = xforms.count();
    file.xforms.resize(nx);
    for (uint32_t i = 0; i < nx; ++i) {
        if (!xforms.is_alive(i)) {
            LOG_ERROR("[Native] transform hole at " << i);
            return false;
        }
        file.xforms[i].parent = xforms.get_parent(i);
        file.xforms[i].trs = xforms.get_local_trs(i);
    }

    file.node_to_xform = renderer.scene_manager.gltf_node_to_transform();
    if (node_names.size() != file.node_to_xform.size()) {
        LOG_ERROR("[Native] node name count " << node_names.size()
                                              << " != transform map "
                                              << file.node_to_xform.size());
        return false;
    }
    file.node_names = node_names;

    const uint32_t ngo = renderer.scene_manager.game_object_count();
    file.game_objects.reserve(ngo);
    for (uint32_t i = 0; i < ngo; ++i) {
        const scene::GameObject& go = renderer.scene_manager.get_game_object(i);
        if (go.flags != 0) {
            LOG_ERROR("[Native] game object '" << go.name << "' has flags set");
            return false;
        }
        file.game_objects.push_back(go);
    }
    const uint32_t nrm = renderer.scene_manager.render_mesh_count();
    file.render_meshes.reserve(nrm);
    for (uint32_t i = 0; i < nrm; ++i) {
        const scene::RenderMesh& rm = renderer.scene_manager.get_render_mesh(i);
        if (rm.flags != 0) {
            LOG_ERROR("[Native] render mesh " << i << " has flags set");
            return false;
        }
        file.render_meshes.push_back(rm);
    }

    const auto& skins = renderer.scene_manager.skins();
    file.skins.reserve(skins.skin_count());
    for (uint32_t i = 0; i < skins.skin_count(); ++i)
        file.skins.push_back(skins.skin(i));

    file.clips = renderer.scene_manager.animations().clips();

    const auto& morphs = renderer.scene_manager.morphs();
    file.morphs.reserve(morphs.instance_count());
    for (uint32_t i = 0; i < morphs.instance_count(); ++i)
        file.morphs.push_back(morphs.instance(i));
    file.node_to_morph = morphs.node_to_morph();

    file.lights = renderer.lights;
    file.global_light = renderer.globalLight;
    file.camera.valid = cam_valid;
    file.camera.yfov = yfov;
    file.camera.aspect = aspect;
    file.camera.znear = znear;
    file.camera.zfar = zfar;
    file.camera.gltf_index = cam_gltf;
    file.camera.transform_index = cam_xform;

    scene::NativeEcs& ecs = file.ecs;
    ecs.entity_count = ecs_world.entity_capacity();
    ecs.alive.assign(ecs.entity_count, 0);
    for (uint32_t i = 0; i < ecs.entity_count; ++i)
        ecs.alive[i] = ecs_world.is_alive(i) ? 1u : 0u;
    if (!copy_pairs(ecs_world.transform_links, ecs.entity_count, ecs.transform_links) ||
        !copy_ids(ecs_world.player_tags, ecs.entity_count, ecs.player_tags) ||
        !copy_ids(ecs_world.desktop_moves, ecs.entity_count, ecs.desktop_moves) ||
        !copy_pairs(ecs_world.fps_moves, ecs.entity_count, ecs.fps_moves) ||
        !copy_pairs(ecs_world.camera_rigs, ecs.entity_count, ecs.camera_rigs) ||
        !copy_pairs(ecs_world.healths, ecs.entity_count, ecs.healths) ||
        !copy_pairs(ecs_world.names, ecs.entity_count, ecs.names) ||
        !copy_pairs(ecs_world.locomotion_anims, ecs.entity_count, ecs.locomotion)) {
        LOG_ERROR("[Native] ECS snapshot has an entity outside the roster");
        return false;
    }
    {
        const auto& ents = ecs_world.scripts.entities();
        const auto& data = ecs_world.scripts.data();
        if (ents.size() != data.size()) {
            LOG_ERROR("[Native] script store is inconsistent");
            return false;
        }
        for (size_t i = 0; i < ents.size(); ++i) {
            if (ents[i] >= ecs.entity_count) {
                LOG_ERROR("[Native] script entity is outside the roster");
                return false;
            }
            ecs.scripts.emplace_back(ents[i], data[i].name);
        }
    }
    if (ecs.entity_count == 0 &&
        (!ecs.transform_links.empty() || !ecs.player_tags.empty() ||
         !ecs.scripts.empty() || !ecs.locomotion.empty())) {
        LOG_ERROR("[Native] ECS components exist with an empty roster");
        return false;
    }
    ecs.gltf_node_to_entity = ecs_world.gltf_node_to_entity;
    ecs.game_object_to_entity = ecs_world.game_object_to_entity;

    if (scene_physics) {
        if (!physics.export_cooked_bodies(file.bodies))
            return false;
    }

    const std::filesystem::path path = scene::native_cache_path(scene_name);
    return scene::write_native_scene(path, scene_name, world_scale,
                                     native_flags(optimize_meshes, scene_physics),
                                     source_size, source_mtime, file);
}
