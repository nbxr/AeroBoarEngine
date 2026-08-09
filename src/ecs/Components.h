#pragma once

#include "ecs/Entity.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>

namespace ecs {

// Index into scene::TransformManager (optional for free-fly player).
struct TransformLink {
    uint32_t transform_index = ~0u;
};

// Controllable player; first entity with this tag is active_player.
struct PlayerTag {};

// Desktop free-fly / look policy for this entity (debug inspection).
struct DesktopMove {};

// First-person grounded controller (WASD walk, mouse look, jump, crouch).
// Prefer this over DesktopMove for the chess player capsule.
struct FpsMove {
    float yaw_deg = 0.0f;   // degrees, world Y
    float pitch_deg = 0.0f; // degrees, clamped
    float vertical_velocity = 0.0f;
    float ground_y = 0.0f; // feet / root rest height (sim meters)
    bool grounded = true;
    bool crouching = false;
    bool initialized = false;

    float jump_speed = 4.5f;       // m/s upward impulse
    float gravity = 18.0f;         // m/s²
    float crouch_eye_scale = 0.55f; // multiplies eye_offset.y while crouching
    float crouch_speed_scale = 0.5f;
    float pitch_min = -89.0f;
    float pitch_max = 89.0f;
};

// Camera tunables owned by the player rig (mirrored onto scene::Camera while active).
struct CameraRig {
    float movement_speed = 5.0f;
    float mouse_sensitivity = 0.35f;
    float roll_speed = 90.0f; // deg/s (free-fly only)
    float fov_degrees = 60.0f;
    float near_plane = 0.01f;
    float far_plane = 1000.0f;
    bool invert_pitch = false;

    // Eye position relative to player root (glTF: +Y up).
    // FPS: camera at root + (0, eye_y, 0) with crouch scale; free-fly uses full offset.
    // Default is a short tabletop eye height so a capsule on a chess board is not
    // glued into the wood; override per-asset via extras (see ecs-plan authoring).
    glm::vec3 eye_offset{0.0f, 0.08f, 0.0f};
};

struct Health {
    float current = 100.0f;
    float max = 100.0f;
};

struct Name {
    std::string value;
};

// Script name + instance handle — instance store lands with Phase 3.
struct Script {
    std::string name;
    uint32_t instance_handle = ~0u;
};

} // namespace ecs
