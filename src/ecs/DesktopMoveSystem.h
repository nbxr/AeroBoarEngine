#pragma once

#include "core/InputFrame.h"
#include "ecs/Entity.h"

namespace scene {
class Camera;
class TransformManager;
}

namespace ecs {

class World;

// Player free-fly + look. Only entities with PlayerTag + DesktopMove.
// Writes scene::Camera (storage option A). If the player has a TransformLink,
// also writes that transform so authored meshes (capsule, etc.) move with the player.
// Y/T adjust movement_speed on the rig.
void desktop_move_system_update(World& world, const core::InputFrame& frame,
                                scene::Camera& camera,
                                scene::TransformManager* transforms = nullptr);

// Copy CameraRig tunables onto scene::Camera (call when activating player / after load).
void sync_camera_from_rig(const World& world, Entity player, scene::Camera& camera);

// Copy scene::Camera tunables into CameraRig (seed default player from load pose).
// Does not overwrite eye_offset (authoring-owned).
void sync_rig_from_camera(World& world, Entity player, const scene::Camera& camera);

// Place camera at player_root + R * CameraRig.eye_offset (and match orientation).
void place_camera_on_player(const World& world, Entity player, scene::Camera& camera,
                            scene::TransformManager& transforms);

} // namespace ecs
