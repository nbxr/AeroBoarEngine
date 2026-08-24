#pragma once

#include "core/InputFrame.h"
#include "ecs/Entity.h"

namespace scene {
class Camera;
class TransformManager;
}

namespace physics {
class PhysicsWorld;
}

namespace ecs {

class World;

// Grounded move for PlayerTag + a live TransformLink (any authored body).
// - Mouse look (yaw/pitch, no roll)
// - WASD on horizontal plane (relative to camera yaw) — moves the character
// - Space jump, Left-Ctrl crouch (hold)
// - Gravity vs static ground (Jolt raycast when PhysicsWorld is passed)
// - CameraRig.third_person: orbit boom + look-at + body yaw; else first-person eye
// Writes player TransformLink translation (+ yaw on root in third-person) and scene::Camera.
void fps_move_system_update(World& world, const core::InputFrame& frame,
                            scene::Camera& camera,
                            scene::TransformManager& transforms,
                            physics::PhysicsWorld* physics = nullptr);

} // namespace ecs
