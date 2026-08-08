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

// Desktop free-fly / look policy for this entity.
struct DesktopMove {};

// Camera tunables owned by the player rig (mirrored onto scene::Camera while active).
struct CameraRig {
    float movement_speed = 5.0f;
    float mouse_sensitivity = 0.35f;
    float roll_speed = 90.0f; // deg/s
    float fov_degrees = 60.0f;
    float near_plane = 0.01f;
    float far_plane = 1000.0f;
    bool invert_pitch = false;

    // Eye position in *player local* space (glTF: +Y up).
    // Camera world ≈ player_root + R * eye_offset.
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
