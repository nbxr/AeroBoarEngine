#include "gfx/Engine.h"
#include "gfx/BufferUtils.h"
#include "gfx/DrawBatch.h"
#include "ecs/Components.h"
#include "ecs/Entity.h"
#include "core/Configuration.h"
#include "core/Log.h"
#include "scene/TransformManager.h"
#include "tiny_gltf.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

void gfx::Engine::step_physics(float delta_time) {
    if (!physics.is_initialized())
        return;
    auto& xforms = renderer.scene_manager.transforms();
    // Player / movers first, then integrate, then push dynamics back to meshes.
    xforms.propagate_if_dirty();
    physics.sync_from_transforms(xforms);
    physics.step(delta_time);
    physics.sync_to_transforms(xforms);
    process_kill_floor();
}

void gfx::Engine::configure_kill_floor() {
    // World-level policy (not ECS components): one plane for the loaded scene.
    // Y is in simulation meters after worldScale.
    kill_floor_enabled_ = true;
    float margin = 2.0f;
    bool have_explicit_y = false;
    float explicit_y = 0.0f;

    const nlohmann::json& cfg = core::Configuration::get_root();
    if (cfg.contains("killFloor") && cfg["killFloor"].is_object()) {
        const auto& kf = cfg["killFloor"];
        if (kf.contains("enabled") && kf["enabled"].is_boolean())
            kill_floor_enabled_ = kf["enabled"].get<bool>();
        if (kf.contains("margin") && kf["margin"].is_number())
            margin = static_cast<float>(kf["margin"].get<double>());
        if (kf.contains("y") && kf["y"].is_number()) {
            have_explicit_y = true;
            explicit_y = static_cast<float>(kf["y"].get<double>());
        }
    } else if (cfg.contains("killFloorY") && cfg["killFloorY"].is_number()) {
        // Flat alias: absolute Y, always on.
        have_explicit_y = true;
        explicit_y = static_cast<float>(cfg["killFloorY"].get<double>());
        kill_floor_enabled_ = true;
    } else if (cfg.contains("killFloorEnabled") &&
               cfg["killFloorEnabled"].is_boolean()) {
        kill_floor_enabled_ = cfg["killFloorEnabled"].get<bool>();
    }

    if (!kill_floor_enabled_) {
        LOG_INFO("[Physics] kill floor disabled");
        return;
    }

    if (have_explicit_y) {
        kill_floor_y_ = explicit_y;
    } else {
        // Auto: below scene framing sphere (already includes worldScale).
        const auto [center, radius] =
            renderer.scene_manager.get_scene_framing_sphere();
        kill_floor_y_ = center.y - radius - margin;
    }

    LOG_INFO("[Physics] kill floor ON at y=" << kill_floor_y_
             << (have_explicit_y ? " (config)" : " (auto scene bounds + margin)")
             << " margin=" << margin);
}

void gfx::Engine::process_kill_floor() {
    if (!kill_floor_enabled_ || !physics.is_initialized())
        return;

    auto& xforms = renderer.scene_manager.transforms();
    uint32_t killed = 0;

    const uint32_t slots = physics.body_slot_count();
    for (physics::BodyHandle h = 0; h < slots; ++h) {
        if (!physics.is_body_alive(h))
            continue;
        // Only free-falling dynamics — leave static board and kinematic player.
        if (physics.get_motion_type(h) != physics::MotionType::Dynamic)
            continue;

        glm::vec3 pos{};
        glm::quat rot{};
        if (!physics.get_pose(h, pos, rot))
            continue;
        if (pos.y >= kill_floor_y_)
            continue;

        const uint32_t ti = physics.get_transform_link(h);
        if (ti != ~0u && xforms.is_alive(ti)) {
            // Hide mesh (and any children that inherit scale) without full GO teardown.
            scene::LocalTrs trs = xforms.get_local_trs(ti);
            trs.scale = glm::vec3(0.0f);
            trs.translation = pos; // last known world-ish pose for roots
            xforms.set_local_trs(ti, trs);
        }

        physics.destroy_body(h);
        ++killed;
    }

    if (killed > 0) {
        xforms.propagate_if_dirty();
        LOG_INFO("[Physics] kill floor removed " << killed
                 << " dynamic body(ies) (y < " << kill_floor_y_ << ")");
    }
}

bool gfx::Engine::rebuild_draw_batches() {
    renderer.mesh_draw_infos.clear();
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
            renderer.mesh_manager.get_primitive_index_count(mesh_idx) > 0)
            mesh_keys.push_back(mesh_idx);
    }
    std::sort(mesh_keys.begin(), mesh_keys.end());

    for (uint32_t mesh_idx : mesh_keys) {
        MeshDrawInfo info{};
        info.mesh_index = mesh_idx;
        info.index_count =
            renderer.mesh_manager.get_primitive_index_count(mesh_idx);
        info.index_offset =
            renderer.mesh_manager.get_primitive_index_offset(mesh_idx);
        info.vertex_offset = static_cast<int32_t>(
            renderer.mesh_manager.get_primitive_vertex_offset(mesh_idx));
        info.render_mesh_ids = std::move(by_mesh[mesh_idx]);
        renderer.mesh_draw_infos.push_back(std::move(info));
    }

    renderer.last_total_render_meshes = n_rm;
    if (!renderer.gpu_culling.build_scene(renderer.vk.device.device,
                                          renderer.allocator,
                                          renderer.mesh_draw_infos,
                                          renderer.scene_manager,
                                          &renderer.material_manager)) {
        LOG_ERROR("[Draw] GPU cull build_scene failed");
        return false;
    }
    wire_hzb_descriptors();

    // Rebind per-frame instance buffers (build_scene may recreate them).
    for (uint32_t i = 0; i < renderer.vk.bindless_descriptor_sets.size(); ++i) {
        auto& set = renderer.vk.bindless_descriptor_sets[i];
        if (renderer.gpu_culling.is_ready()) {
            auto& inst = renderer.gpu_culling.out_instances(i);
            gfx::BufferUtils::update_descriptor(
                renderer.vk.device.device, inst, set, inst.info.size,
                Renderer::BINDING_DRAW_INSTANCES);
        }
    }

    renderer.transform_upload_mask =
        (1u << Renderer::MAX_FRAMES_IN_FLIGHT) - 1u;

    LOG_INFO("[Draw] GPU cull ready: " << n_rm << " renderMeshes / "
             << renderer.scene_manager.game_object_count() << " gameObjects / "
             << renderer.mesh_draw_infos.size() << " batches");
    return true;
}

namespace {

// Collect POSITION attributes from all primitives of a glTF mesh (local space).
std::vector<glm::vec3> mesh_positions(const tinygltf::Model& model, int mesh_index) {
    std::vector<glm::vec3> pts;
    if (mesh_index < 0 || mesh_index >= static_cast<int>(model.meshes.size()))
        return pts;
    const auto& mesh = model.meshes[static_cast<size_t>(mesh_index)];
    for (const auto& prim : mesh.primitives) {
        auto it = prim.attributes.find("POSITION");
        if (it == prim.attributes.end())
            continue;
        const auto& acc = model.accessors[static_cast<size_t>(it->second)];
        if (acc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
            acc.type != TINYGLTF_TYPE_VEC3 || acc.bufferView < 0)
            continue;
        const auto& bv = model.bufferViews[static_cast<size_t>(acc.bufferView)];
        const auto& buf = model.buffers[static_cast<size_t>(bv.buffer)];
        const int stride =
            acc.ByteStride(bv) ? acc.ByteStride(bv) : 12;
        const uint8_t* base =
            buf.data.data() + bv.byteOffset + acc.byteOffset;
        pts.reserve(pts.size() + acc.count);
        for (size_t i = 0; i < acc.count; ++i) {
            const float* f =
                reinterpret_cast<const float*>(base + i * static_cast<size_t>(stride));
            pts.emplace_back(f[0], f[1], f[2]);
        }
    }
    return pts;
}

void fill_pose_from_node(physics::BodyPoseDesc& pose, const glm::mat4& world,
                         const scene::LocalTrs& local_trs) {
    pose.position = glm::vec3(world[3]);
    // Use local rotation composed with parent: extract from world, remove scale.
    glm::vec3 c0(world[0]), c1(world[1]), c2(world[2]);
    const float s0 = glm::length(c0);
    const float s1 = glm::length(c1);
    const float s2 = glm::length(c2);
    if (s0 > 1e-8f)
        c0 /= s0;
    if (s1 > 1e-8f)
        c1 /= s1;
    if (s2 > 1e-8f)
        c2 /= s2;
    pose.rotation = glm::normalize(glm::quat_cast(glm::mat3(c0, c1, c2)));
    (void)local_trs;
}

} // namespace

bool gfx::Engine::spawn_scene_physics(const tinygltf::Model& model) {
    if (!physics.is_initialized()) {
        if (!physics.initialize()) {
            LOG_ERROR("[Physics] failed to initialize for scene physics");
            return false;
        }
    }

    // Match load_scene worldScale: linear sizes come from scaled transforms;
    // mass scales with volume (S^3) so density stays roughly constant.
    float world_scale = 1.0f;
    {
        const nlohmann::json& cfg = core::Configuration::get_root();
        if (cfg.contains("worldScale") && cfg["worldScale"].is_number())
            world_scale = static_cast<float>(cfg["worldScale"].get<double>());
        else if (cfg.contains("debugWorldScale") &&
                 cfg["debugWorldScale"].is_number())
            world_scale =
                static_cast<float>(cfg["debugWorldScale"].get<double>());
        if (world_scale < 1e-6f)
            world_scale = 1.0f;
    }
    const float mass_scale = world_scale * world_scale * world_scale;

    auto& scene = renderer.scene_manager;
    auto& xforms = scene.transforms();
    xforms.propagate_if_dirty();
    const auto& node_to_x = scene.gltf_node_to_transform();

    // Player transforms: force kinematic (gameplay owns pose).
    std::unordered_map<uint32_t, bool> player_xforms;
    for (ecs::Entity pe : ecs_world.player_tags.entities()) {
        if (const ecs::TransformLink* link = ecs_world.transform_links.try_get(pe)) {
            if (link->transform_index != ~0u)
                player_xforms[link->transform_index] = true;
        }
    }

    // Root-level materials (friction / restitution).
    std::vector<std::pair<float, float>> mats; // friction, restitution
    {
        auto it = model.extensions.find("KHR_physics_rigid_bodies");
        if (it != model.extensions.end() && it->second.IsObject()) {
            const auto& root = it->second.Get<tinygltf::Value::Object>();
            auto mit = root.find("physicsMaterials");
            if (mit != root.end() && mit->second.IsArray()) {
                for (const auto& m : mit->second.Get<tinygltf::Value::Array>()) {
                    if (!m.IsObject())
                        continue;
                    const auto& mo = m.Get<tinygltf::Value::Object>();
                    float fr = 0.5f, re = 0.1f;
                    auto f1 = mo.find("dynamicFriction");
                    if (f1 != mo.end() && f1->second.IsNumber())
                        fr = static_cast<float>(f1->second.GetNumberAsDouble());
                    auto f2 = mo.find("staticFriction");
                    if (f2 != mo.end() && f2->second.IsNumber())
                        fr = std::max(
                            fr, static_cast<float>(f2->second.GetNumberAsDouble()));
                    auto r = mo.find("restitution");
                    if (r != mo.end() && r->second.IsNumber())
                        re = static_cast<float>(r->second.GetNumberAsDouble());
                    mats.emplace_back(fr, re);
                }
            }
        }
    }

    // KHR_implicit_shapes root array.
    const tinygltf::Value* shapes_arr = nullptr;
    {
        auto it = model.extensions.find("KHR_implicit_shapes");
        if (it != model.extensions.end() && it->second.IsObject()) {
            const auto& root = it->second.Get<tinygltf::Value::Object>();
            auto sit = root.find("shapes");
            if (sit != root.end() && sit->second.IsArray())
                shapes_arr = &sit->second;
        }
    }

    uint32_t n_static = 0, n_dynamic = 0, n_kinematic = 0, n_fail = 0;

    for (size_t ni = 0; ni < model.nodes.size(); ++ni) {
        const auto& node = model.nodes[ni];
        auto eit = node.extensions.find("KHR_physics_rigid_bodies");
        if (eit == node.extensions.end() || !eit->second.IsObject())
            continue;

        if (ni >= node_to_x.size() ||
            node_to_x[ni] == scene::TransformManager::kInvalid) {
            ++n_fail;
            continue;
        }
        const uint32_t xform = node_to_x[ni];
        const scene::LocalTrs& local_trs = xforms.get_local_trs(xform);
        const glm::mat4& world = xforms.get_world_matrix(xform);
        const glm::vec3 scl = glm::abs(local_trs.scale);

        const auto& rb = eit->second.Get<tinygltf::Value::Object>();

        // Motion → dynamic (or kinematic for ECS player / isKinematic).
        physics::MotionType motion = physics::MotionType::Static;
        float mass = 0.0f;
        auto mo = rb.find("motion");
        if (mo != rb.end() && mo->second.IsObject()) {
            motion = physics::MotionType::Dynamic;
            const auto& mobj = mo->second.Get<tinygltf::Value::Object>();
            auto mass_it = mobj.find("mass");
            if (mass_it != mobj.end() && mass_it->second.IsNumber())
                mass = static_cast<float>(mass_it->second.GetNumberAsDouble());
            auto kin = mobj.find("isKinematic");
            if (kin != mobj.end() && kin->second.IsBool() && kin->second.Get<bool>())
                motion = physics::MotionType::Kinematic;
        }
        if (mass > 0.0f && std::abs(mass_scale - 1.0f) > 1e-5f)
            mass *= mass_scale;
        if (player_xforms.count(xform))
            motion = physics::MotionType::Kinematic;

        // Collider
        auto col = rb.find("collider");
        if (col == rb.end() || !col->second.IsObject()) {
            ++n_fail;
            continue;
        }
        const auto& cobj = col->second.Get<tinygltf::Value::Object>();

        float friction = 0.5f, restitution = 0.1f;
        auto mat_it = cobj.find("physicsMaterial");
        if (mat_it != cobj.end() && mat_it->second.IsNumber()) {
            const int mi = mat_it->second.GetNumberAsInt();
            if (mi >= 0 && mi < static_cast<int>(mats.size())) {
                friction = mats[static_cast<size_t>(mi)].first;
                restitution = mats[static_cast<size_t>(mi)].second;
            }
        }

        // geometry: nested object (ABeautifulGameScene) or flat fields
        const tinygltf::Value::Object* geom = nullptr;
        auto git = cobj.find("geometry");
        if (git != cobj.end() && git->second.IsObject())
            geom = &git->second.Get<tinygltf::Value::Object>();
        else
            geom = &cobj;

        physics::BodyPoseDesc pose{};
        fill_pose_from_node(pose, world, local_trs);
        pose.motion = motion;
        pose.mass = mass;
        pose.friction = friction;
        pose.restitution = restitution;
        if (motion != physics::MotionType::Static)
            pose.transform_index = xform;

        physics::BodyHandle body = physics::kInvalidBody;

        // Implicit shape index
        auto shape_it = geom->find("shape");
        if (shape_it != geom->end() && shape_it->second.IsNumber() && shapes_arr) {
            const int si = shape_it->second.GetNumberAsInt();
            const auto& sarr = shapes_arr->Get<tinygltf::Value::Array>();
            if (si >= 0 && si < static_cast<int>(sarr.size()) && sarr[si].IsObject()) {
                const auto& sh = sarr[si].Get<tinygltf::Value::Object>();
                std::string type;
                auto tit = sh.find("type");
                if (tit != sh.end() && tit->second.IsString())
                    type = tit->second.Get<std::string>();

                if (type == "capsule" || sh.count("capsule")) {
                    auto cit = sh.find("capsule");
                    const tinygltf::Value::Object* cap =
                        (cit != sh.end() && cit->second.IsObject())
                            ? &cit->second.Get<tinygltf::Value::Object>()
                            : &sh;
                    float height = 1.0f, r_bot = 0.25f, r_top = 0.25f;
                    auto h = cap->find("height");
                    if (h != cap->end() && h->second.IsNumber())
                        height = std::abs(
                            static_cast<float>(h->second.GetNumberAsDouble()));
                    auto rb2 = cap->find("radiusBottom");
                    if (rb2 != cap->end() && rb2->second.IsNumber())
                        r_bot = static_cast<float>(rb2->second.GetNumberAsDouble());
                    auto rt = cap->find("radiusTop");
                    if (rt != cap->end() && rt->second.IsNumber())
                        r_top = static_cast<float>(rt->second.GetNumberAsDouble());
                    // Shapes are in node local space — apply node scale.
                    const float s = std::max({scl.x, scl.y, scl.z});
                    physics::CapsuleDesc capd{};
                    static_cast<physics::BodyPoseDesc&>(capd) = pose;
                    capd.radius = std::max(0.5f * (r_bot + r_top) * s, 1e-3f);
                    // KHR height is full cylindrical length; Jolt wants half.
                    capd.half_height = std::max(0.5f * height * s, 1e-3f);
                    body = physics.create_capsule(capd);
                } else if (type == "box" || sh.count("box")) {
                    auto bit = sh.find("box");
                    const tinygltf::Value::Object* boxo =
                        (bit != sh.end() && bit->second.IsObject())
                            ? &bit->second.Get<tinygltf::Value::Object>()
                            : &sh;
                    glm::vec3 size(1.0f);
                    auto sit2 = boxo->find("size");
                    if (sit2 != boxo->end() && sit2->second.IsArray()) {
                        const auto& a = sit2->second.Get<tinygltf::Value::Array>();
                        if (a.size() >= 3) {
                            size = glm::vec3(
                                static_cast<float>(a[0].GetNumberAsDouble()),
                                static_cast<float>(a[1].GetNumberAsDouble()),
                                static_cast<float>(a[2].GetNumberAsDouble()));
                        }
                    }
                    physics::BoxDesc box{};
                    static_cast<physics::BodyPoseDesc&>(box) = pose;
                    box.half_extents = glm::max(0.5f * size * scl, glm::vec3(1e-3f));
                    body = physics.create_box(box);
                } else if (type == "sphere" || sh.count("sphere")) {
                    auto spit = sh.find("sphere");
                    const tinygltf::Value::Object* sp =
                        (spit != sh.end() && spit->second.IsObject())
                            ? &spit->second.Get<tinygltf::Value::Object>()
                            : &sh;
                    float radius = 0.5f;
                    auto rit = sp->find("radius");
                    if (rit != sp->end() && rit->second.IsNumber())
                        radius = static_cast<float>(rit->second.GetNumberAsDouble());
                    // Approximate sphere with box for now (no SphereShape helper).
                    physics::BoxDesc box{};
                    static_cast<physics::BodyPoseDesc&>(box) = pose;
                    const float r = std::max(radius * std::max({scl.x, scl.y, scl.z}), 1e-3f);
                    box.half_extents = glm::vec3(r);
                    body = physics.create_box(box);
                }
            }
        }

        // Mesh / convex hull geometry (ABeautifulGame pieces + board).
        if (body == physics::kInvalidBody) {
            auto mesh_it = geom->find("mesh");
            if (mesh_it != geom->end() && mesh_it->second.IsNumber()) {
                const int mesh_idx = mesh_it->second.GetNumberAsInt();
                bool convex = true;
                auto ch = geom->find("convexHull");
                if (ch != geom->end() && ch->second.IsBool())
                    convex = ch->second.Get<bool>();

                std::vector<glm::vec3> pts = mesh_positions(model, mesh_idx);
                if (!pts.empty()) {
                    // Bake node scale into points (body pose is unscaled T+R).
                    for (glm::vec3& p : pts) {
                        p.x *= scl.x;
                        p.y *= scl.y;
                        p.z *= scl.z;
                    }
                    if (convex || true) { // always hull for MVP (no mesh collider)
                        physics::ConvexHullDesc hull{};
                        static_cast<physics::BodyPoseDesc&>(hull) = pose;
                        hull.points = std::move(pts);
                        body = physics.create_convex_hull(hull);
                    }
                }
            }
        }

        if (body == physics::kInvalidBody) {
            ++n_fail;
            continue;
        }
        if (motion == physics::MotionType::Static)
            ++n_static;
        else if (motion == physics::MotionType::Kinematic)
            ++n_kinematic;
        else
            ++n_dynamic;
    }

    LOG_INFO("[Physics] KHR_physics_rigid_bodies: static=" << n_static
             << " dynamic=" << n_dynamic << " kinematic=" << n_kinematic
             << " failed=" << n_fail
             << " total_bodies=" << physics.body_count()
             << " worldScale=" << world_scale
             << " massScale=" << mass_scale);
    return physics.body_count() > 0;
}
