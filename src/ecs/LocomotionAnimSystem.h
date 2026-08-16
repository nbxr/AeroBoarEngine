#pragma once

namespace tinygltf {
class Model;
}

namespace scene {
class AnimationSystem;
class SceneManager;
}

namespace ecs {

class World;

// After extras are applied: mask player-object TRS + skeleton "root" translation.
void bind_player_animation_masks(World& world, scene::SceneManager& scene,
                                 const tinygltf::Model& model);

// Resolve clip names → indices and play idle (call once after clips load).
void locomotion_anim_bind_clips(World& world, scene::AnimationSystem& anims);

// After FpsMove: pick Stand/Walk/Run from horizontal_speed and crossfade.
void locomotion_anim_system_update(World& world, scene::AnimationSystem& anims);

} // namespace ecs
