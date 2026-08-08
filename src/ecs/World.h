#pragma once

#include "ecs/ComponentStore.h"
#include "ecs/Components.h"
#include "ecs/Entity.h"
#include "ecs/ScriptInstanceStore.h"
#include <vector>

namespace ecs {

// Custom ECS world. GameObject/render tables stay in scene/gfx until M3.
class World {
  public:
    World() = default;

    Entity create_entity();
    void destroy_entity(Entity e);
    [[nodiscard]] bool is_alive(Entity e) const;

    // First PlayerTag in spawn order (first wins).
    void resolve_active_player();
    [[nodiscard]] Entity active_player() const { return active_player_; }
    void set_active_player(Entity e) { active_player_ = e; }

    // Spawn free-fly player (PlayerTag + DesktopMove + CameraRig).
    Entity spawn_default_desktop_player(const CameraRig& rig = CameraRig{});

    ComponentStore<TransformLink> transform_links;
    ComponentStore<PlayerTag> player_tags;
    ComponentStore<DesktopMove> desktop_moves;
    ComponentStore<CameraRig> camera_rigs;
    ComponentStore<Health> healths;
    ComponentStore<Name> names;
    ComponentStore<Script> scripts;

    ScriptInstanceStore script_instances;

    // Dual-write maps (gltf / GameObject → Entity). Cleared with world.
    std::vector<Entity> gltf_node_to_entity;
    std::vector<Entity> game_object_to_entity;

    void clear();

  private:
    std::vector<bool> alive_;
    std::vector<Entity> free_list_;
    Entity active_player_ = kInvalidEntity;
};

} // namespace ecs
