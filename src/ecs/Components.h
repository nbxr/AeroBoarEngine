#pragma once

#include "ecs/Entity.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
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

    // Authored local rotation captured on first third-person tick (Y-yaw only).
    glm::quat body_rest_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool body_rest_captured = false;

    // Horizontal speed applied this tick (m/s). LocomotionAnim reads this.
    float horizontal_speed = 0.0f;
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

    // Third-person follow (extras: "camera": "third_person", "boom_offset": [r, up, back]).
    // boom_offset is sim meters (after worldScale). Not multiplied at load.
    bool third_person = false;
    glm::vec3 boom_offset{0.0f, 1.6f, 3.0f}; // right, up, back (sim meters)
};

// Idle / walk / run driven by FpsMove.horizontal_speed. extras type
// "locomotion_anim" (also accepts typo "locomation_anim").
struct LocomotionAnim {
    enum class State : uint8_t { Stand = 0, Walk = 1, Run = 2 };

    uint32_t idle_clip = ~0u;
    uint32_t walk_clip = ~0u;
    uint32_t run_clip = ~0u;
    float walk_threshold = 0.05f; // m/s — any WASD
    float run_threshold = 1.0e9f; // omitted extras → no run unless authored
    float fade = 0.2f;
    State state = State::Stand;
    bool clips_bound = false;

    std::string idle_name = "T-Pose";
    std::string walk_name = "Walk";
    std::string run_name = "Run";
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
