#pragma once

#include "core/InputFrame.h"
#include "ecs/Entity.h"

namespace scene {
class Camera;
class TransformManager;
}

namespace ecs {

class World;

// Grounded move for PlayerTag + FpsMove.
// - Mouse look (yaw/pitch, no roll)
// - WASD on horizontal plane (relative to camera yaw)
// - Space jump, Left-Ctrl crouch (hold)
// - Simple gravity vs ground_y (no raycast yet); kinematic body follows transform
// - CameraRig.third_person: orbit boom + look-at + body yaw; else first-person eye
// Writes player TransformLink translation (+ yaw on root in third-person) and scene::Camera.
void fps_move_system_update(World& world, const core::InputFrame& frame,
                            scene::Camera& camera,
                            scene::TransformManager& transforms);

} // namespace ecs
