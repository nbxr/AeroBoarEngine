#include "ecs/GltfEcsLoader.h"
#include "ecs/ScriptSystem.h"
#include "ecs/World.h"
#include "scene/SceneManager.h"
#include "core/Log.h"

#include <tiny_gltf.h>
#include <cstdio>

namespace ecs {
namespace {

const tinygltf::Value* find_ecs_array(const tinygltf::Value& extras) {
    if (!extras.IsObject())
        return nullptr;
    const auto& obj = extras.Get<tinygltf::Value::Object>();
    auto it = obj.find("ECS_Components_v1");
    if (it == obj.end() || !it->second.IsArray())
        return nullptr;
    return &it->second;
}

float get_number(const tinygltf::Value::Object& obj, const char* key, float def) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->second.IsNumber())
        return def;
    return static_cast<float>(it->second.GetNumberAsDouble());
}

std::string get_string(const tinygltf::Value::Object& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->second.IsString())
        return {};
    return it->second.Get<std::string>();
}

bool parse_vec3(const tinygltf::Value& v, glm::vec3& out) {
    if (v.IsArray()) {
        const auto& arr = v.Get<tinygltf::Value::Array>();
        if (arr.size() >= 3 && arr[0].IsNumber() && arr[1].IsNumber() &&
            arr[2].IsNumber()) {
            out = glm::vec3(static_cast<float>(arr[0].GetNumberAsDouble()),
                            static_cast<float>(arr[1].GetNumberAsDouble()),
                            static_cast<float>(arr[2].GetNumberAsDouble()));
            return true;
        }
        return false;
    }
    if (!v.IsString())
        return false;
    float x = 0.f, y = 0.f, z = 0.f;
    if (std::sscanf(v.Get<std::string>().c_str(), " [ %f , %f , %f ]", &x, &y,
                    &z) >= 3 ||
        std::sscanf(v.Get<std::string>().c_str(), "[%f,%f,%f]", &x, &y, &z) >=
            3 ||
        std::sscanf(v.Get<std::string>().c_str(), "%f,%f,%f", &x, &y, &z) >= 3) {
        out = glm::vec3(x, y, z);
        return true;
    }
    return false;
}

uint32_t apply_component_entry(World& world, Entity entity,
                               const tinygltf::Value& entry) {
    if (!entry.IsObject())
        return 0;
    const auto& obj = entry.Get<tinygltf::Value::Object>();
    const std::string type = get_string(obj, "type");
    if (type.empty()) {
        LOG_ERROR("[ECS] ECS_Components_v1 entry missing \"type\"");
        return 0;
    }

    if (type == "player") {
        world.player_tags.get_or_emplace(entity);
        // Grounded FPS for demo player; free-fly still available via DesktopMove only.
        world.fps_moves.get_or_emplace(entity);
        CameraRig& rig = world.camera_rigs.get_or_emplace(entity);
        // Optional eye height: "eye_offset": [x,y,z] or string "[x, y, z]"
        auto eit = obj.find("eye_offset");
        if (eit != obj.end())
            parse_vec3(eit->second, rig.eye_offset);
        auto hit = obj.find("eye_height");
        if (hit != obj.end() && hit->second.IsNumber()) {
            rig.eye_offset = glm::vec3(
                0.0f, static_cast<float>(hit->second.GetNumberAsDouble()), 0.0f);
        }

        const std::string cam = get_string(obj, "camera");
        if (cam == "third_person" || cam == "thirdperson" || cam == "3rd")
            rig.third_person = true;
        else if (cam == "first_person" || cam == "firstperson" || cam == "fps")
            rig.third_person = false;
        auto bit = obj.find("boom_offset");
        if (bit != obj.end() && parse_vec3(bit->second, rig.boom_offset))
            rig.third_person = true; // boom implies follow cam
        if (rig.third_person) {
            LOG_INFO("[ECS] player third_person boom=("
                     << rig.boom_offset.x << ", " << rig.boom_offset.y << ", "
                     << rig.boom_offset.z << ")");
        }
        return 1;
    }
    if (type == "health") {
        Health h{};
        h.current = get_number(obj, "current", 100.0f);
        h.max = get_number(obj, "max", h.current);
        world.healths.get_or_emplace(entity, h);
        return 1;
    }
    if (type == "script") {
        Script sc{};
        sc.name = get_string(obj, "name");
        if (sc.name.empty()) {
            LOG_ERROR("[ECS] script component missing \"name\"");
            return 0;
        }
        world.scripts.get_or_emplace(entity, sc);
        script_system_bind(world, entity);
        return 1;
    }
    if (type == "name") {
        Name n{};
        n.value = get_string(obj, "value");
        if (n.value.empty())
            n.value = get_string(obj, "name");
        world.names.get_or_emplace(entity, n);
        return 1;
    }
    if (type == "locomotion_anim" || type == "locomation_anim") {
        LocomotionAnim loco{};
        const std::string idle = get_string(obj, "idle");
        const std::string walk = get_string(obj, "walk");
        const std::string run = get_string(obj, "run");
        if (!idle.empty())
            loco.idle_name = idle;
        if (!walk.empty())
            loco.walk_name = walk;
        if (!run.empty())
            loco.run_name = run;
        auto wit = obj.find("walk_speed");
        if (wit != obj.end() && wit->second.IsNumber())
            loco.walk_threshold =
                static_cast<float>(wit->second.GetNumberAsDouble());
        auto rit = obj.find("run_speed");
        if (rit != obj.end() && rit->second.IsNumber())
            loco.run_threshold =
                static_cast<float>(rit->second.GetNumberAsDouble());
        auto fit = obj.find("fade");
        if (fit != obj.end() && fit->second.IsNumber())
            loco.fade = static_cast<float>(fit->second.GetNumberAsDouble());
        world.locomotion_anims.get_or_emplace(entity, loco);
        LOG_INFO("[ECS] locomotion_anim idle='" << loco.idle_name << "' walk='"
                 << loco.walk_name << "' run='" << loco.run_name
                 << "' fade=" << loco.fade);
        return 1;
    }

    LOG_INFO("[ECS] Unknown ECS_Components_v1 type '" << type << "' (skipped)");
    return 0;
}

} // namespace

uint32_t apply_ecs_components_v1(World& world, Entity entity,
                                 const tinygltf::Model& model, int node_index) {
    if (node_index < 0 || node_index >= static_cast<int>(model.nodes.size()))
        return 0;
    const auto& node = model.nodes[static_cast<size_t>(node_index)];
    const tinygltf::Value* arr = find_ecs_array(node.extras);
    if (!arr)
        return 0;

    uint32_t applied = 0;
    const auto& list = arr->Get<tinygltf::Value::Array>();
    for (const auto& entry : list)
        applied += apply_component_entry(world, entity, entry);
    return applied;
}

uint32_t populate_world_from_gltf(World& world, const tinygltf::Model& model,
                                  scene::SceneManager& scene) {
    world.clear();
    world.gltf_node_to_entity.assign(model.nodes.size(), kInvalidEntity);

    // Dual-write: Entity per mesh GameObject.
    const uint32_t go_count = scene.game_object_count();
    for (uint32_t gi = 0; gi < go_count; ++gi) {
        const auto& go = scene.get_game_object(gi);
        Entity e = world.create_entity();
        if (go.root_transform_index != ~0u) {
            TransformLink link{};
            link.transform_index = go.root_transform_index;
            world.transform_links.get_or_emplace(e, link);
        }
        if (go.gltf_node_index != ~0u &&
            go.gltf_node_index < world.gltf_node_to_entity.size()) {
            world.gltf_node_to_entity[go.gltf_node_index] = e;
            if (go.gltf_node_index < model.nodes.size() &&
                !model.nodes[go.gltf_node_index].name.empty()) {
                world.names.get_or_emplace(
                    e, Name{model.nodes[go.gltf_node_index].name});
            }
        }
        world.game_object_to_entity.push_back(e);
    }

    // Non-mesh nodes (or any node) with ECS extras: ensure entity exists.
    uint32_t extras_hits = 0;
    for (size_t ni = 0; ni < model.nodes.size(); ++ni) {
        if (!find_ecs_array(model.nodes[ni].extras))
            continue;

        Entity e = kInvalidEntity;
        if (ni < world.gltf_node_to_entity.size())
            e = world.gltf_node_to_entity[ni];

        if (e == kInvalidEntity) {
            e = world.create_entity();
            const auto& node_to_x = scene.gltf_node_to_transform();
            if (ni < node_to_x.size() &&
                node_to_x[ni] != scene::TransformManager::kInvalid) {
                TransformLink link{};
                link.transform_index = node_to_x[ni];
                world.transform_links.get_or_emplace(e, link);
            }
            if (!model.nodes[ni].name.empty())
                world.names.get_or_emplace(e, Name{model.nodes[ni].name});
            world.gltf_node_to_entity[ni] = e;
        }

        extras_hits += apply_ecs_components_v1(world, e, model, static_cast<int>(ni));
    }

    world.resolve_active_player();

    LOG_INFO("[ECS] populate_world_from_gltf: game_objects=" << go_count
             << " ecs_component_entries=" << extras_hits
             << " players=" << world.player_tags.size()
             << " active_player="
             << (world.active_player() == kInvalidEntity
                     ? -1
                     : static_cast<int>(world.active_player())));
    return extras_hits;
}

} // namespace ecs
