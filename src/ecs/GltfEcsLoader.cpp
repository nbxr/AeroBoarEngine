#include "ecs/GltfEcsLoader.h"
#include "ecs/ScriptSystem.h"
#include "ecs/World.h"
#include "scene/SceneManager.h"
#include "core/Log.h"

#include <tiny_gltf.h>
#include <nlohmann/json.hpp>
#include <cstdio>

namespace ecs {
namespace {

tinygltf::Value json_to_tiny(const nlohmann::json& j) {
    if (j.is_object()) {
        tinygltf::Value::Object o;
        for (auto it = j.begin(); it != j.end(); ++it)
            o.emplace(it.key(), json_to_tiny(it.value()));
        return tinygltf::Value(std::move(o));
    }
    if (j.is_array()) {
        tinygltf::Value::Array a;
        a.reserve(j.size());
        for (const auto& e : j)
            a.push_back(json_to_tiny(e));
        return tinygltf::Value(std::move(a));
    }
    if (j.is_string())
        return tinygltf::Value(j.get<std::string>());
    if (j.is_boolean())
        return tinygltf::Value(j.get<bool>());
    if (j.is_number_integer())
        return tinygltf::Value(j.get<int>());
    if (j.is_number())
        return tinygltf::Value(j.get<double>());
    return {};
}

tinygltf::Value parse_extras_json(const std::string& s) {
    if (s.empty())
        return {};
    const nlohmann::json j = nlohmann::json::parse(s, nullptr, false);
    if (j.is_discarded())
        return {};
    return json_to_tiny(j);
}

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
                     << rig.boom_offset.z << ") entity=" << entity);
        } else {
            LOG_INFO("[ECS] player first_person entity=" << entity);
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

const tinygltf::Value* extras_object(const tinygltf::Node& node,
                                     tinygltf::Value& storage) {
    if (node.extras.IsObject())
        return &node.extras;
    if (node.extras.IsString()) {
        storage = parse_extras_json(node.extras.Get<std::string>());
        return storage.IsObject() ? &storage : nullptr;
    }
    if (!node.extras_json_string.empty()) {
        storage = parse_extras_json(node.extras_json_string);
        return storage.IsObject() ? &storage : nullptr;
    }
    return nullptr;
}

bool has_settings_components(const tinygltf::Value* extras) {
    if (!extras || !extras->IsObject())
        return false;
    const auto& obj = extras->Get<tinygltf::Value::Object>();
    auto it = obj.find("ecs_components_settings");
    if (it == obj.end() || !it->second.IsObject())
        return false;
    const auto& s = it->second.Get<tinygltf::Value::Object>();
    auto cit = s.find("components");
    return cit != s.end() && cit->second.IsArray();
}

// Blender UI writes props here; overlay after ECS_Components_v1 so edits apply.
uint32_t apply_settings_overlay(World& world, Entity entity,
                                const tinygltf::Value& extras) {
    if (!extras.IsObject())
        return 0;
    const auto& obj = extras.Get<tinygltf::Value::Object>();
    auto sit = obj.find("ecs_components_settings");
    if (sit == obj.end() || !sit->second.IsObject())
        return 0;
    const auto& settings = sit->second.Get<tinygltf::Value::Object>();
    auto cit = settings.find("components");
    if (cit == settings.end() || !cit->second.IsArray())
        return 0;

    uint32_t applied = 0;
    for (const auto& comp : cit->second.Get<tinygltf::Value::Array>()) {
        if (!comp.IsObject())
            continue;
        const auto& cobj = comp.Get<tinygltf::Value::Object>();
        const std::string type = get_string(cobj, "type");
        auto pit = cobj.find("props");
        if (type.empty() || pit == cobj.end() || !pit->second.IsArray())
            continue;

        tinygltf::Value::Object synthetic;
        synthetic.emplace("type", tinygltf::Value(type));
        for (const auto& prop : pit->second.Get<tinygltf::Value::Array>()) {
            if (!prop.IsObject())
                continue;
            const auto& pobj = prop.Get<tinygltf::Value::Object>();
            const std::string key = get_string(pobj, "key");
            auto vit = pobj.find("value");
            if (key.empty() || vit == pobj.end())
                continue;
            synthetic.emplace(key, vit->second);
        }
        applied += apply_component_entry(world, entity, tinygltf::Value(synthetic));
    }
    return applied;
}

} // namespace

uint32_t apply_ecs_components_v1(World& world, Entity entity,
                                 const tinygltf::Model& model, int node_index) {
    if (node_index < 0 || node_index >= static_cast<int>(model.nodes.size()))
        return 0;
    const auto& node = model.nodes[static_cast<size_t>(node_index)];
    tinygltf::Value storage;
    const tinygltf::Value* extras = extras_object(node, storage);
    const tinygltf::Value* arr = extras ? find_ecs_array(*extras) : nullptr;

    uint32_t applied = 0;
    if (extras)
        applied += apply_settings_overlay(world, entity, *extras);
    if (arr) {
        const auto& list = arr->Get<tinygltf::Value::Array>();
        for (const auto& entry : list)
            applied += apply_component_entry(world, entity, entry);
    }
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
    const auto& node_to_x = scene.gltf_node_to_transform();
    for (size_t ni = 0; ni < model.nodes.size(); ++ni) {
        tinygltf::Value extras_storage;
        const tinygltf::Value* extras =
            extras_object(model.nodes[ni], extras_storage);
        const tinygltf::Value* arr = extras ? find_ecs_array(*extras) : nullptr;
        if (!arr && !has_settings_components(extras))
            continue;

        Entity e = kInvalidEntity;
        if (ni < world.gltf_node_to_entity.size())
            e = world.gltf_node_to_entity[ni];

        if (e == kInvalidEntity) {
            e = world.create_entity();
            if (!model.nodes[ni].name.empty())
                world.names.get_or_emplace(e, Name{model.nodes[ni].name});
            world.gltf_node_to_entity[ni] = e;
        }

        // Always bind the glTF node transform so WASD has a body to move.
        if (!world.transform_links.has(e) && ni < node_to_x.size() &&
            node_to_x[ni] != scene::TransformManager::kInvalid) {
            TransformLink link{};
            link.transform_index = node_to_x[ni];
            world.transform_links.get_or_emplace(e, link);
        }

        uint32_t applied = 0;
        // Blender UI props first; ECS_Components_v1 last so a hand-edited
        // boom_offset array wins over a stale ecs_components_settings string.
        if (extras)
            applied += apply_settings_overlay(world, e, *extras);
        if (arr) {
            const auto& list = arr->Get<tinygltf::Value::Array>();
            for (const auto& entry : list)
                applied += apply_component_entry(world, e, entry);
        }
        extras_hits += applied;

        if (const TransformLink* link = world.transform_links.try_get(e)) {
            LOG_INFO("[ECS] extras node=" << ni << " '"
                     << model.nodes[ni].name << "' entity=" << e
                     << " xform=" << link->transform_index
                     << " entries=" << applied);
        } else {
            LOG_ERROR("[ECS] extras node=" << ni << " '"
                      << model.nodes[ni].name << "' entity=" << e
                      << " has no TransformLink — WASD cannot move this body");
        }
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
