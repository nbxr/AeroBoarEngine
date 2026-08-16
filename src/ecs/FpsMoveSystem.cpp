#include "ecs/FpsMoveSystem.h"
#include "ecs/DesktopMoveSystem.h"
#include "ecs/World.h"
#include "scene/Camera.h"
#include "scene/TransformManager.h"
#include "core/Log.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace ecs {
namespace {

float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

// Yaw (around Y) then pitch (around local X), camera looks along -Z.
glm::quat orientation_from_yaw_pitch(float yaw_deg, float pitch_deg) {
    const glm::quat yaw_q =
        glm::angleAxis(glm::radians(yaw_deg), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat pitch_q =
        glm::angleAxis(glm::radians(pitch_deg), glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::normalize(yaw_q * pitch_q);
}

void init_from_camera_and_root(FpsMove& fps, const scene::Camera& camera,
                               const glm::vec3& root_pos) {
    // Recover yaw/pitch from camera forward for a seamless handoff from free-fly.
    glm::vec3 fwd = camera.get_forward();
    if (glm::dot(fwd, fwd) < 1e-10f)
        fwd = glm::vec3(0.0f, 0.0f, -1.0f);
    fwd = glm::normalize(fwd);
    // yaw=0 → look -Z; yaw increases CCW (to +X).
    fps.yaw_deg = glm::degrees(std::atan2(fwd.x, -fwd.z));
    fps.pitch_deg =
        clampf(glm::degrees(std::asin(clampf(fwd.y, -1.0f, 1.0f))), fps.pitch_min,
               fps.pitch_max);
    fps.ground_y = root_pos.y;
    fps.vertical_velocity = 0.0f;
    fps.grounded = true;
    fps.crouching = false;
    fps.initialized = true;
    // Scale jump with locomotion speed so worldScale stays coherent.
    if (camera.movement_speed > 0.1f) {
        fps.jump_speed = std::max(2.0f, camera.movement_speed * 0.85f);
        fps.gravity = std::max(12.0f, fps.jump_speed * 4.0f);
    }
    LOG_INFO("[FPS] init yaw=" << fps.yaw_deg << " pitch=" << fps.pitch_deg
             << " ground_y=" << fps.ground_y << " jump=" << fps.jump_speed
             << " walk=" << camera.movement_speed);
}

} // namespace

void fps_move_system_update(World& world, const core::InputFrame& frame,
                            scene::Camera& camera,
                            scene::TransformManager& transforms) {
    Entity player = world.active_player();
    if (player == kInvalidEntity || !world.is_alive(player))
        return;
    if (!world.player_tags.has(player) || !world.fps_moves.has(player))
        return;

    TransformLink* link = world.transform_links.try_get(player);
    if (!link || link->transform_index == ~0u ||
        !transforms.is_alive(link->transform_index)) {
        // No body: caller should use free-fly DesktopMove instead of FPS.
        return;
    }

    FpsMove& fps = world.fps_moves.get_or_emplace(player);
    CameraRig* rig = world.camera_rigs.try_get(player);

    // Speed / sensitivity from rig (Y/T adjust walk speed).
    constexpr float kSpeedMin = 0.01f;
    constexpr float kSpeedMax = 200.0f;
    constexpr float kSpeedStep = 1.25f;
    if (rig) {
        if (frame.y_pressed) {
            rig->movement_speed =
                std::min(kSpeedMax, rig->movement_speed * kSpeedStep);
            camera.movement_speed = rig->movement_speed;
            LOG_INFO("[FPS] movement_speed=" << camera.movement_speed);
        }
        if (frame.t_pressed) {
            rig->movement_speed =
                std::max(kSpeedMin, rig->movement_speed / kSpeedStep);
            camera.movement_speed = rig->movement_speed;
            LOG_INFO("[FPS] movement_speed=" << camera.movement_speed);
        }
        camera.mouse_sensitivity = rig->mouse_sensitivity;
        camera.invert_pitch = rig->invert_pitch;
        camera.fov_degrees = rig->fov_degrees;
        camera.near_plane = rig->near_plane;
        camera.far_plane = rig->far_plane;
    }

    const uint32_t ti = link->transform_index;
    transforms.propagate_if_dirty();
    glm::vec3 root = glm::vec3(transforms.get_world_matrix(ti)[3]);

    if (!fps.initialized)
        init_from_camera_and_root(fps, camera, root);

    const float dt = std::max(frame.delta_time, 0.0f);

    // --- Look (update yaw/pitch, then camera orientation first) ---
    if (frame.cursor_captured) {
        const float sens = camera.mouse_sensitivity;
        // Match free-fly: mouse right → look right (negative yaw about +Y).
        fps.yaw_deg -= frame.mouse_delta.x * sens;
        const float pitch_sign = camera.invert_pitch ? 1.0f : -1.0f;
        fps.pitch_deg += pitch_sign * frame.mouse_delta.y * sens;
        fps.pitch_deg = clampf(fps.pitch_deg, fps.pitch_min, fps.pitch_max);
        if (fps.yaw_deg > 180.0f)
            fps.yaw_deg -= 360.0f;
        if (fps.yaw_deg < -180.0f)
            fps.yaw_deg += 360.0f;
    }

    // Apply look before movement so W follows the *current* view.
    camera.set_orientation(
        orientation_from_yaw_pitch(fps.yaw_deg, fps.pitch_deg));

    // Horizontal basis from yaw (independent of boom look-at / shoulder offset).
    const glm::quat yaw_q =
        glm::angleAxis(glm::radians(fps.yaw_deg), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 forward = yaw_q * glm::vec3(0.0f, 0.0f, -1.0f);
    forward.y = 0.0f;
    if (glm::dot(forward, forward) < 1e-8f)
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    else
        forward = glm::normalize(forward);
    const glm::vec3 right =
        glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    const bool third_person = rig && rig->third_person;

    // --- Crouch (hold Ctrl) ---
    fps.crouching = frame.left_control;

    // --- Horizontal move (relative to look) ---
    glm::vec3 wish(0.0f);
    if (frame.w)
        wish += forward;
    if (frame.s)
        wish -= forward;
    if (frame.d)
        wish += right;
    if (frame.a)
        wish -= right;
    if (glm::dot(wish, wish) > 1e-8f)
        wish = glm::normalize(wish);

    float speed = camera.movement_speed;
    if (fps.crouching)
        speed *= fps.crouch_speed_scale;
    fps.horizontal_speed = (glm::dot(wish, wish) > 1e-8f) ? speed : 0.0f;
    root += wish * (speed * dt);

    // --- Jump / gravity ---
    if (frame.space_pressed && fps.grounded && !fps.crouching) {
        fps.vertical_velocity = fps.jump_speed;
        fps.grounded = false;
    }

    if (!fps.grounded) {
        fps.vertical_velocity -= fps.gravity * dt;
        root.y += fps.vertical_velocity * dt;
        if (root.y <= fps.ground_y) {
            root.y = fps.ground_y;
            fps.vertical_velocity = 0.0f;
            fps.grounded = true;
        }
    } else {
        root.y = fps.ground_y;
        fps.vertical_velocity = 0.0f;
    }

    // Write root transform: translation always; yaw the body in third-person
    // so Walk/Run face the camera heading. First-person keeps authored rotation
    // (Blender capsules were not always Y-up-clean).
    scene::LocalTrs trs = transforms.get_local_trs(ti);
    if (third_person) {
        if (!fps.body_rest_captured) {
            fps.body_rest_rotation = trs.rotation;
            fps.body_rest_captured = true;
        }
        glm::vec3 rest_fwd = fps.body_rest_rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        rest_fwd.y = 0.0f;
        float rest_yaw = 0.0f;
        if (glm::dot(rest_fwd, rest_fwd) > 1e-8f) {
            rest_fwd = glm::normalize(rest_fwd);
            rest_yaw = glm::degrees(std::atan2(rest_fwd.x, -rest_fwd.z));
        }
        const glm::quat yaw_delta = glm::angleAxis(
            glm::radians(fps.yaw_deg - rest_yaw), glm::vec3(0.0f, 1.0f, 0.0f));
        trs.rotation = glm::normalize(yaw_delta * fps.body_rest_rotation);
    }
    const uint32_t parent = transforms.get_parent(ti);
    if (parent == scene::TransformManager::kInvalid) {
        trs.translation = root;
        transforms.set_local_trs(ti, trs);
    } else {
        transforms.propagate_if_dirty();
        const glm::mat4 inv_parent =
            glm::inverse(transforms.get_world_matrix(parent));
        trs.translation = glm::vec3(inv_parent * glm::vec4(root, 1.0f));
        transforms.set_local_trs(ti, trs);
    }

    // --- Camera ---
    if (third_person) {
        glm::vec3 boom = rig->boom_offset;
        if (fps.crouching)
            boom.y *= fps.crouch_eye_scale;
        apply_follow_boom(camera, root, boom);
    } else {
        glm::vec3 eye_off =
            rig ? rig->eye_offset : glm::vec3(0.0f, 0.08f, 0.0f);
        if (fps.crouching)
            eye_off.y *= fps.crouch_eye_scale;

        // Eye = root + world-up height + look-relative horizontal offset.
        const glm::vec3 eye = root + right * eye_off.x +
                             glm::vec3(0.0f, 1.0f, 0.0f) * eye_off.y -
                             forward * eye_off.z;
        camera.set_position(eye);
    }
}

} // namespace ecs
