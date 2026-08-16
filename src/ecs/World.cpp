#include "ecs/World.h"
#include "ecs/ScriptSystem.h"
#include "core/Log.h"

namespace ecs {

Entity World::create_entity() {
    Entity e = kInvalidEntity;
    if (!free_list_.empty()) {
        e = free_list_.back();
        free_list_.pop_back();
        if (e < alive_.size())
            alive_[e] = true;
    } else {
        e = static_cast<Entity>(alive_.size());
        alive_.push_back(true);
    }
    return e;
}

void World::destroy_entity(Entity e) {
    if (!is_alive(e))
        return;
    script_system_unbind(*this, e);
    transform_links.remove(e);
    player_tags.remove(e);
    desktop_moves.remove(e);
    fps_moves.remove(e);
    camera_rigs.remove(e);
    healths.remove(e);
    names.remove(e);
    scripts.remove(e);
    locomotion_anims.remove(e);
    alive_[e] = false;
    free_list_.push_back(e);
    if (active_player_ == e)
        active_player_ = kInvalidEntity;
}

bool World::is_alive(Entity e) const {
    return e < alive_.size() && alive_[e];
}

void World::resolve_active_player() {
    active_player_ = kInvalidEntity;
    // Dense player_tags order = insertion order → first wins.
    const auto& ents = player_tags.entities();
    if (!ents.empty())
        active_player_ = ents.front();
}

Entity World::spawn_default_desktop_player(const CameraRig& rig) {
    Entity e = create_entity();
    player_tags.get_or_emplace(e);
    // Free-fly for scene inspection when no authored body (no TransformLink).
    // Authored glTF "player" gets FpsMove in GltfEcsLoader instead.
    desktop_moves.get_or_emplace(e);
    camera_rigs.get_or_emplace(e, rig);
    names.get_or_emplace(e, Name{"default_desktop_player"});
    resolve_active_player();
    LOG_INFO("[ECS] Spawned default free-fly player entity=" << e
             << " (active_player=" << active_player_ << ")");
    return e;
}

void World::clear() {
    // Unbind all scripts before wiping stores.
    const auto ents = scripts.entities();
    for (Entity e : ents)
        script_system_unbind(*this, e);
    script_instances.clear();

    transform_links.clear();
    player_tags.clear();
    desktop_moves.clear();
    fps_moves.clear();
    camera_rigs.clear();
    healths.clear();
    names.clear();
    scripts.clear();
    locomotion_anims.clear();
    gltf_node_to_entity.clear();
    game_object_to_entity.clear();
    alive_.clear();
    free_list_.clear();
    active_player_ = kInvalidEntity;
}

} // namespace ecs

