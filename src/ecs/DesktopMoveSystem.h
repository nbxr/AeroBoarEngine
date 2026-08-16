#pragma once

#include "core/InputFrame.h"
#include "ecs/Entity.h"
#include <glm/glm.hpp>

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

// Place camera at player_root + eye_offset (FPS) or follow boom (third-person).
void place_camera_on_player(const World& world, Entity player, scene::Camera& camera,
                            scene::TransformManager& transforms);

// Orbit boom: look-at (root + up * boom.y); eye = target - forward * boom.z + right * boom.x.
// Uses camera's current orientation as the orbit look. boom is (right, up, back) meters.
void apply_follow_boom(scene::Camera& camera, const glm::vec3& root,
                       const glm::vec3& boom_offset);

} // namespace ecs
