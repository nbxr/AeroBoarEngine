#include "ecs/DesktopMoveSystem.h"
#include "ecs/FpsMoveSystem.h"
#include "ecs/World.h"
#include "scene/Camera.h"
#include "scene/TransformManager.h"
#include "core/Log.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace ecs {
namespace {

glm::vec3 eye_offset_or_default(const CameraRig* rig) {
    if (rig)
        return rig->eye_offset;
    return glm::vec3(0.0f, 1.6f, 0.0f);
}

// Only move the player root in world space. Keep authored rotation/scale so a
// correctly oriented body is not forced to camera yaw.
//
// eye = root + world_up * eye_height + horizontal offset in XZ from camera yaw.
// For MVP we apply eye_offset mostly as world-Y height (and optional local XZ
// rotated by camera yaw only for horizontal slide).
void sync_player_transform_from_camera(scene::TransformManager& xforms,
                                       uint32_t transform_index,
                                       const scene::Camera& camera,
                                       const glm::vec3& eye_offset_local) {
    if (!xforms.is_alive(transform_index))
        return;

    // Horizontal forward from camera (yaw only) for optional XZ offset.
    glm::vec3 look = camera.get_forward();
    look.y = 0.0f;
    if (glm::dot(look, look) < 1e-8f)
        look = glm::vec3(0.0f, 0.0f, -1.0f);
    look = glm::normalize(look);
    const glm::vec3 right = glm::normalize(glm::cross(look, glm::vec3(0, 1, 0)));

    // root = eye - (right*ox + up*oy + (-look)*oz) with up fixed world Y.
    // Using world-up for Y avoids tilting the offset when looking up/down.
    const glm::vec3 eye = camera.get_position();
    const glm::vec3 root_world =
        eye - right * eye_offset_local.x - glm::vec3(0, 1, 0) * eye_offset_local.y +
        look * eye_offset_local.z;

    scene::LocalTrs trs = xforms.get_local_trs(transform_index);
    // Preserve authored rotation + scale completely.
    const glm::quat authored_rot = trs.rotation;
    const glm::vec3 authored_scale = trs.scale;

    const uint32_t parent = xforms.get_parent(transform_index);
    if (parent == scene::TransformManager::kInvalid) {
        trs.translation = root_world;
        trs.rotation = authored_rot;
        trs.scale = authored_scale;
        xforms.set_local_trs(transform_index, trs);
        return;
    }

    // Convert desired world translation into local under parent, keep local R/S.
    xforms.propagate_if_dirty();
    const glm::mat4& parent_world = xforms.get_world_matrix(parent);
    const glm::mat4 inv_parent = glm::inverse(parent_world);
    const glm::vec3 local_pos =
        glm::vec3(inv_parent * glm::vec4(root_world, 1.0f));
    trs.translation = local_pos;
    trs.rotation = authored_rot;
    trs.scale = authored_scale;
    xforms.set_local_trs(transform_index, trs);
}

} // namespace

void sync_camera_from_rig(const World& world, Entity player, scene::Camera& camera) {
    const CameraRig* rig = world.camera_rigs.try_get(player);
    if (!rig)
        return;
    camera.movement_speed = rig->movement_speed;
    camera.mouse_sensitivity = rig->mouse_sensitivity;
    camera.roll_speed = rig->roll_speed;
    camera.fov_degrees = rig->fov_degrees;
    camera.near_plane = rig->near_plane;
    camera.far_plane = rig->far_plane;
    camera.invert_pitch = rig->invert_pitch;
}

void sync_rig_from_camera(World& world, Entity player, const scene::Camera& camera) {
    CameraRig& rig = world.camera_rigs.get_or_emplace(player);
    rig.movement_speed = camera.movement_speed;
    rig.mouse_sensitivity = camera.mouse_sensitivity;
    rig.roll_speed = camera.roll_speed;
    rig.fov_degrees = camera.fov_degrees;
    rig.near_plane = camera.near_plane;
    rig.far_plane = camera.far_plane;
    rig.invert_pitch = camera.invert_pitch;
}

void apply_follow_boom(scene::Camera& camera, const glm::vec3& root,
                       const glm::vec3& boom_offset) {
    const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
    const glm::vec3 target = root + world_up * boom_offset.y;

    glm::vec3 look = camera.get_forward();
    if (glm::dot(look, look) < 1e-10f)
        look = glm::vec3(0.0f, 0.0f, -1.0f);
    else
        look = glm::normalize(look);

    glm::vec3 up_ref = world_up;
    if (std::abs(glm::dot(look, world_up)) > 0.999f)
        up_ref = glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 right = glm::normalize(glm::cross(look, up_ref));

    // Z is distance *behind* the current look (WASD heading). Negative Z used
    // to orbit 180° and made W/S feel reversed — always treat as behind.
    const float back = std::abs(boom_offset.z);
    const glm::vec3 eye = target - look * back + right * boom_offset.x;
    glm::vec3 to_target = target - eye;
    if (glm::dot(to_target, to_target) < 1e-10f)
        to_target = look;
    camera.set_position_and_forward(eye, to_target);
}

void place_camera_on_player(const World& world, Entity player,
                            scene::Camera& camera,
                            scene::TransformManager& xforms) {
    const TransformLink* link = world.transform_links.try_get(player);
    if (!link || link->transform_index == ~0u ||
        !xforms.is_alive(link->transform_index))
        return;

    xforms.propagate_if_dirty();
    const glm::mat4& w = xforms.get_world_matrix(link->transform_index);
    const glm::vec3 root_pos(w[3]);

    const CameraRig* rig = world.camera_rigs.try_get(player);
    if (rig && rig->third_person) {
        apply_follow_boom(camera, root_pos, rig->boom_offset);
        const glm::vec3 eye = camera.get_position();
        LOG_INFO("[ECS] place_camera_on_player third_person root=("
                 << root_pos.x << ", " << root_pos.y << ", " << root_pos.z
                 << ") eye=(" << eye.x << ", " << eye.y << ", " << eye.z
                 << ") boom=(" << rig->boom_offset.x << ", " << rig->boom_offset.y
                 << ", " << rig->boom_offset.z << ") xform="
                 << link->transform_index);
        return;
    }

    // Keep current / framed look direction; only lift the eye above the root.
    const glm::vec3 offset = eye_offset_or_default(rig);
    // World-Y eye height from feet (do not use full mesh rotation on offset —
    // avoids inverted/sideways eyes if the node basis is odd).
    const glm::vec3 eye = root_pos + glm::vec3(offset.x, offset.y, offset.z);

    camera.set_position(eye);
    // Leave orientation as the load framing / glTF camera unless nearly unset.
    // Horizontal default look if forward is degenerate.
    glm::vec3 fwd = camera.get_forward();
    if (glm::dot(fwd, fwd) < 1e-8f)
        camera.set_position_and_forward(eye, glm::vec3(0, 0, -1));
    else
        camera.set_position(eye);

    LOG_INFO("[ECS] place_camera_on_player root=("
             << root_pos.x << ", " << root_pos.y << ", " << root_pos.z
             << ") eye=(" << eye.x << ", " << eye.y << ", " << eye.z
             << ") offset=(" << offset.x << ", " << offset.y << ", " << offset.z
             << ") xform=" << link->transform_index);
}

void desktop_move_system_update(World& world, const core::InputFrame& frame,
                                scene::Camera& camera,
                                scene::TransformManager* transforms) {
    Entity player = world.active_player();
    if (player == kInvalidEntity || !world.is_alive(player))
        return;
    if (!world.player_tags.has(player))
        return;

    // Follow-cam / authored body: never 6DOF-fly. WASD must move the character.
    const CameraRig* rig_tp = world.camera_rigs.try_get(player);
    const TransformLink* body = world.transform_links.try_get(player);
    const bool has_body =
        body && body->transform_index != ~0u && transforms &&
        transforms->is_alive(body->transform_index);
    if (has_body && (world.fps_moves.has(player) ||
                     (rig_tp && rig_tp->third_person))) {
        world.fps_moves.get_or_emplace(player);
        fps_move_system_update(world, frame, camera, *transforms);
        return;
    }

    if (!world.desktop_moves.has(player))
        return;

    constexpr float kSpeedMin = 0.01f;
    constexpr float kSpeedMax = 200.0f;
    constexpr float kSpeedStep = 1.25f;
    const CameraRig* rig_c = world.camera_rigs.try_get(player);
    if (CameraRig* rig = world.camera_rigs.try_get(player)) {
        if (frame.y_pressed) {
            rig->movement_speed =
                std::min(kSpeedMax, rig->movement_speed * kSpeedStep);
            camera.movement_speed = rig->movement_speed;
            LOG_INFO("[Camera] movement_speed=" << camera.movement_speed);
        }
        if (frame.t_pressed) {
            rig->movement_speed =
                std::max(kSpeedMin, rig->movement_speed / kSpeedStep);
            camera.movement_speed = rig->movement_speed;
            LOG_INFO("[Camera] movement_speed=" << camera.movement_speed);
        }
        camera.mouse_sensitivity = rig->mouse_sensitivity;
        camera.roll_speed = rig->roll_speed;
        camera.invert_pitch = rig->invert_pitch;
    }

    camera.apply_desktop_input(frame);

    if (!transforms) {
        static bool once = false;
        if (!once) {
            LOG_ERROR("[ECS] desktop_move: TransformManager* is null");
            once = true;
        }
        return;
    }

    if (const TransformLink* link = world.transform_links.try_get(player)) {
        if (link->transform_index != ~0u &&
            transforms->is_alive(link->transform_index)) {
            sync_player_transform_from_camera(*transforms, link->transform_index,
                                              camera,
                                              eye_offset_or_default(rig_c));
        } else {
            static bool once = false;
            if (!once) {
                LOG_ERROR("[ECS] active player TransformLink invalid xform="
                          << (link ? link->transform_index : ~0u));
                once = true;
            }
        }
    } else {
        static bool once = false;
        if (!once) {
            LOG_ERROR(
                "[ECS] active player entity="
                << player
                << " has no TransformLink — put extras.player on the body "
                   "root (mesh node or parent empty that owns the hierarchy)");
            once = true;
        }
    }
}

} // namespace ecs
