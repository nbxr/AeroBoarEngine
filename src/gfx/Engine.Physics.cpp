#include "gfx/Engine.h"
#include "gfx/BufferUtils.h"
#include "gfx/DrawBatch.h"
#include "gfx/Material.h"
#include "gfx/MeshData.h"
#include "gfx/Vertex.h"
#include "core/Log.h"

#include <algorithm>
#include <cstring>
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
    physics.step(delta_time);
    physics.sync_to_transforms(renderer.scene_manager.transforms());
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
