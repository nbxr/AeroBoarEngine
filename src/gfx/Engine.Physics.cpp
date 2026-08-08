#include "gfx/Engine.h"
#include "gfx/BufferUtils.h"
#include "gfx/DrawBatch.h"
#include "gfx/Material.h"
#include "gfx/MeshData.h"
#include "gfx/Vertex.h"
#include "ecs/Components.h"
#include "ecs/Entity.h"
#include "core/Configuration.h"
#include "core/Log.h"
#include "tiny_gltf.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void pack_uv0(gfx::Vertex& v, float u, float vcoord) {
    const uint16_t uu =
        static_cast<uint16_t>(std::clamp(u, 0.0f, 1.0f) * 65535.0f + 0.5f);
    const uint16_t vv =
        static_cast<uint16_t>(std::clamp(vcoord, 0.0f, 1.0f) * 65535.0f + 0.5f);
    v.uv[0] = static_cast<uint8_t>(uu & 0xFF);
    v.uv[1] = static_cast<uint8_t>((uu >> 8) & 0xFF);
    v.uv[2] = static_cast<uint8_t>(vv & 0xFF);
    v.uv[3] = static_cast<uint8_t>((vv >> 8) & 0xFF);
    v.uv[4] = v.uv[0];
    v.uv[5] = v.uv[1];
    v.uv[6] = v.uv[2];
    v.uv[7] = v.uv[3];
}

gfx::Vertex make_vertex(const glm::vec3& p, const glm::vec3& n, float u,
                        float v) {
    gfx::Vertex out{};
    out.position[0] = p.x;
    out.position[1] = p.y;
    out.position[2] = p.z;
    out.normal[0] = n.x;
    out.normal[1] = n.y;
    out.normal[2] = n.z;
    // Tangent +handedness
    out.tangent[0] = 1.0f;
    out.tangent[1] = 0.0f;
    out.tangent[2] = 0.0f;
    out.tangent[3] = 1.0f;
    pack_uv0(out, u, v);
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 255;
    out.blend_weights[0] = 255;
    out.blend_weights[1] = out.blend_weights[2] = out.blend_weights[3] = 0;
    out.blend_indices[0] = out.blend_indices[1] = out.blend_indices[2] =
        out.blend_indices[3] = 0;
    return out;
}

// Unit cube centered at origin, half-extent 0.5 (matches BoxDesc default).
gfx::MeshData make_unit_cube_mesh() {
    gfx::MeshData mesh{};
    const float h = 0.5f;
    struct Face {
        glm::vec3 n;
        glm::vec3 p[4];
    };
    const Face faces[6] = {
        {{0, 0, 1},
         {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}},
        {{0, 0, -1},
         {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}},
        {{1, 0, 0},
         {{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}}},
        {{-1, 0, 0},
         {{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}},
        {{0, 1, 0},
         {{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}},
        {{0, -1, 0},
         {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}},
    };

    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);
    for (const Face& f : faces) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(make_vertex(f.p[0], f.n, 0.0f, 0.0f));
        mesh.vertices.push_back(make_vertex(f.p[1], f.n, 1.0f, 0.0f));
        mesh.vertices.push_back(make_vertex(f.p[2], f.n, 1.0f, 1.0f));
        mesh.vertices.push_back(make_vertex(f.p[3], f.n, 0.0f, 1.0f));
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }
    mesh.local_aabb.min = glm::vec3(-h);
    mesh.local_aabb.max = glm::vec3(h);
    return mesh;
}

} // namespace

void gfx::Engine::step_physics(float delta_time) {
    if (!physics.is_initialized())
        return;
    auto& xforms = renderer.scene_manager.transforms();
    // Player / movers first, then integrate, then push dynamics back to meshes.
    xforms.propagate_if_dirty();
    physics.sync_from_transforms(xforms);
    physics.step(delta_time);
    physics.sync_to_transforms(xforms);
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

bool gfx::Engine::spawn_physics_demo() {
    if (!physics.is_initialized()) {
        if (!physics.initialize()) {
            LOG_ERROR("[Physics] failed to initialize for demo");
            return false;
        }
    }

    // Static floor (physics only — no mesh yet).
    const physics::BodyHandle floor =
        physics.create_floor(/*half_xz=*/20.0f, /*half_h=*/0.5f, /*y=*/-0.5f);
    if (floor == physics::kInvalidBody) {
        LOG_ERROR("[Physics] floor create failed");
        return false;
    }

    // Procedural unit cube + simple orange PBR material (no textures).
    gfx::MeshData cube = make_unit_cube_mesh();
    const core::AABB cube_aabb = cube.local_aabb;
    const gfx::MeshPrimitiveID mesh_id = renderer.mesh_manager.add_mesh(cube);

    gfx::Material mat{};
    mat.albedo = glm::vec4(0.92f, 0.45f, 0.12f, 1.0f);
    mat.roughness = 0.45f;
    mat.metallic = 0.05f;
    mat.emissive_factor = glm::vec4(0.f, 0.f, 0.f, 1.f);
    mat.normalStrength = 1.0f;
    mat.flags = 0;
    const gfx::MaterialID mat_id = renderer.material_manager.create_material(mat);

    auto& xforms = renderer.scene_manager.transforms();

    // A few falling boxes at different heights / x offsets.
    constexpr int kBoxes = 5;
    const glm::vec3 starts[kBoxes] = {
        {0.0f, 4.0f, 0.0f},
        {1.2f, 5.5f, 0.3f},
        {-1.0f, 6.0f, -0.5f},
        {0.5f, 7.2f, 1.0f},
        {-0.7f, 8.0f, 0.2f},
    };

    for (int i = 0; i < kBoxes; ++i) {
        const uint32_t ti = xforms.allocate();
        scene::LocalTrs trs{};
        trs.translation = starts[i];
        trs.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        trs.scale = glm::vec3(1.0f);
        xforms.set_local_trs(ti, trs);
        xforms.set_parent(ti, scene::TransformManager::kInvalid);

        const uint32_t go =
            renderer.scene_manager.create_game_object(ti, /*gltf_node=*/~0u);
        if (go == ~0u) {
            LOG_ERROR("[Physics] create_game_object failed");
            continue;
        }
        renderer.scene_manager.add_render_mesh(go, static_cast<uint32_t>(mesh_id),
                                               static_cast<uint32_t>(mat_id),
                                               cube_aabb, ti);

        physics::BoxDesc desc{};
        desc.half_extents = glm::vec3(0.5f);
        desc.position = starts[i];
        desc.motion = physics::MotionType::Dynamic;
        desc.restitution = 0.35f;
        desc.friction = 0.6f;
        desc.mass = 1.0f;
        desc.transform_index = ti;
        const physics::BodyHandle body = physics.create_box(desc);
        if (body == physics::kInvalidBody)
            LOG_ERROR("[Physics] dynamic box create failed");
    }

    xforms.mark_all_dirty();
    xforms.propagate();
    renderer.scene_manager.refresh_instance_worlds();

    // Upload new mesh / material / instances to the inactive buffer side, then
    // flip so they are visible next frame.
    renderer.mesh_manager.update_buffers();
    renderer.material_manager.update_buffers();
    renderer.scene_manager.update_buffers();
    renderer.mesh_manager.toggle_buffers();
    renderer.material_manager.toggle_buffers();
    renderer.scene_manager.toggle_buffers();

    for (uint32_t i = 0; i < renderer.vk.bindless_descriptor_sets.size(); ++i) {
        auto& set = renderer.vk.bindless_descriptor_sets[i];
        renderer.material_manager.bind_descriptor(2, set);
        renderer.mesh_manager.bind_descriptor(3, 4, 5, set);
    }

    if (!rebuild_draw_batches())
        return false;

    LOG_INFO("[Physics] demo spawned: floor + " << kBoxes
             << " dynamic boxes (bodies=" << physics.body_count() << ")");
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
