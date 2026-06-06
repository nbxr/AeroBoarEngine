#include "scene/Camera.h"
#include "core/InputManager.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <cstdio>
#include <algorithm>

namespace scene {

Camera::Camera(GLFWwindow* glfwWindow)
    : window(glfwWindow)
{
    // Better default position for typical loaded scenes (e.g. DamagedHelmet at ~origin)
    position = {0.0f, 3.0f, 10.0f};
    yaw = -90.0f;
    pitch = -10.0f;

    // Initialize quaternion from default yaw/pitch
    orientation = glm::angleAxis(glm::radians(yaw), glm::vec3(0,1,0)) *
                  glm::angleAxis(glm::radians(pitch), glm::vec3(1,0,0));
}

void Camera::set_mode(CameraMode new_mode) {
    mode = new_mode;

    // Cursor capture is now driven via core::InputManager::set_cursor_captured()
    // (called from the main loop or higher input handling). This keeps
    // scene::Camera decoupled from GLFW details.
}

void Camera::update(float delta_time, core::InputManager& input) {
    if (mode == CameraMode::Desktop) {
        update_desktop(delta_time, input);
    }
    // VR mode: view matrix is set externally via set_vr_view_matrix()
}

void Camera::update_desktop(float delta_time, core::InputManager& input) {
    if (!window) return;

    // Get current basis from orientation
    glm::vec3 front = glm::normalize(orientation * glm::vec3(0, 0, -1)); // -Z is forward in our convention
    glm::vec3 right = glm::normalize(orientation * glm::vec3(1, 0, 0));
    glm::vec3 camera_up = glm::normalize(orientation * glm::vec3(0, 1, 0));

    float velocity = movement_speed * delta_time;

    // === Keyboard movement (now fully camera-relative) ===
    if (input.is_key_down(GLFW_KEY_W))
        position += front * velocity;
    if (input.is_key_down(GLFW_KEY_S))
        position -= front * velocity;
    if (input.is_key_down(GLFW_KEY_A))
        position -= right * velocity;
    if (input.is_key_down(GLFW_KEY_D))
        position += right * velocity;
    if (input.is_key_down(GLFW_KEY_SPACE))
        position += camera_up * velocity;
    if (input.is_key_down(GLFW_KEY_LEFT_SHIFT))
        position -= camera_up * velocity;

    // === Q/E Roll (around camera forward) ===
    if (input.is_key_down(GLFW_KEY_Q)) {
        // Q = roll counterclockwise from the user's perspective when looking forward
        glm::quat roll = glm::angleAxis(glm::radians(-roll_speed * delta_time), front);
        orientation = glm::normalize(roll * orientation);
    }
    if (input.is_key_down(GLFW_KEY_E)) {
        // E = roll clockwise from the user's perspective
        glm::quat roll = glm::angleAxis(glm::radians(+roll_speed * delta_time), front);
        orientation = glm::normalize(roll * orientation);
    }

    // === Mouse look (only while cursor is captured; deltas come pre-smoothed/accelerated from InputManager) ===
    if (input.is_cursor_captured()) {
        glm::vec2 mouse_delta = input.get_mouse_delta();
        float xoffset = mouse_delta.x * mouse_sensitivity;

        // Negate Y delta to match the original comfortable desktop sign convention
        // (last_y - current_y behavior from the pre-refactor polling code).
        // With invert_pitch=false (default): mouse down → camera looks down.
        float yoffset = -mouse_delta.y * mouse_sensitivity;

        // 1. Mouse X now yaws around the camera's current Up
        if (xoffset != 0.0f) {
            glm::quat yawRot = glm::angleAxis(glm::radians(-xoffset), camera_up);
            orientation = glm::normalize(yawRot * orientation);
        }

        // 2. Mouse Y pitches around the camera's current Right.
        // invert_pitch controls whether mouse down makes the camera look up or down.
        if (yoffset != 0.0f) {
            float pitch_sign = invert_pitch ? -1.0f : +1.0f;
            glm::quat pitchRot = glm::angleAxis(glm::radians(pitch_sign * yoffset), right);
            orientation = glm::normalize(pitchRot * orientation);
        }
    }
}

glm::mat4 Camera::get_view_matrix() const {
    if (mode == CameraMode::VR) {
        return vr_view_matrix;
    }

    // Build view matrix from current orientation + position
    glm::mat4 rotation = glm::mat4_cast(glm::inverse(orientation));
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), -position);
    return rotation * translation;
}

glm::mat4 Camera::get_projection_matrix(float aspect_ratio) const {
    return glm::perspective(glm::radians(fov_degrees), aspect_ratio, near_plane, far_plane);
}

void Camera::set_vr_view_matrix(const glm::mat4& view_matrix) {
    vr_view_matrix = view_matrix;
}

void Camera::set_position(const glm::vec3& pos) {
    position = pos;
}

glm::vec3 Camera::get_forward() const {
    return glm::normalize(orientation * glm::vec3(0.0f, 0.0f, -1.0f));
}

void Camera::reset_mouse_state() {
    // Delegate to InputManager (which owns the tracking baseline and delta clearing)
    // so that capture toggles (Escape etc.) never produce jumps.
    core::InputManager::get_instance().reset_mouse_state();
}

void Camera::frame(const glm::vec3& center, float radius) {
    // Nice offset angle (slightly from above and to the side)
    float distance = radius * 2.8f + 1.5f;   // some padding
    glm::vec3 offset = glm::normalize(glm::vec3(0.7f, 0.9f, 1.3f)) * distance;

    position = center + offset;

    glm::vec3 dir = glm::normalize(center - position);

    // Build orientation that looks along `dir` with world up as reference
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 newFront = -dir; // our convention: -Z is forward
    glm::vec3 newRight = glm::normalize(glm::cross(newFront, worldUp));
    glm::vec3 newUp = glm::cross(newRight, newFront);

    // Construct quaternion from the basis
    glm::mat3 rotMat(newRight, newUp, newFront);
    orientation = glm::quat_cast(rotMat);

    // Update legacy yaw/pitch for compatibility
    yaw = glm::degrees(std::atan2(dir.z, dir.x));
    pitch = glm::degrees(std::asin(dir.y));
}

void Camera::set_from_camera_node(const glm::mat4& world_transform,
                                  float yfov_radians,
                                  float znear,
                                  float zfar,
                                  float aspect_ratio)
{
    // Position comes from the translation column of the node transform
    position = glm::vec3(world_transform[3]);

    // The upper 3x3 of the camera node's world transform is *already* the correct
    // rotation matrix that takes the camera's local axes to world space.
    //
    // glTF camera convention (see KHR_lights_punctual and core glTF spec):
    //   - The node's transform orients the camera in the scene.
    //   - In the camera's local space, +Z points backward; therefore the camera
    //     "looks" along local -Z.
    //   - After the world transform, the look direction in world space is
    //     therefore - (third column of the rotation part).
    //
    // scene::Camera convention (used by update_desktop, get_view_matrix, frame, etc.):
    //   - "front" (the direction the camera is facing) is defined as
    //     orientation * vec3(0, 0, -1)
    //
    // These two conventions line up perfectly. Therefore the correct implementation
    // is simply:
    //     orientation = quat_cast( mat3( camera_node_world_transform ) )
    //
    // No manual basis reconstruction or sign flipping is required.
    glm::mat3 cameraRotation = glm::mat3(world_transform);
    orientation = glm::quat_cast(cameraRotation);

    // Derive look direction (for legacy yaw/pitch and diagnostics only)
    glm::vec3 gltfLookDir = glm::normalize( -glm::vec3(cameraRotation[2]) );

    // Update legacy yaw/pitch for any code that still reads them (best-effort only)
    yaw   = glm::degrees(std::atan2(gltfLookDir.z, gltfLookDir.x));
    pitch = glm::degrees(std::asin(gltfLookDir.y));

    // Apply projection parameters from the glTF camera
    if (yfov_radians > 0.0f)
        fov_degrees = glm::degrees(yfov_radians);

    if (aspect_ratio > 0.0f) {
        // glTF provided an explicit aspectRatio for this camera.
        // We currently always derive the projection from the runtime window aspect
        // (so the view direction/position are correct, but the exact framing may
        // differ if the window does not match the authored aspect).
        // Future work: store the value and either force an initial window size
        // or build a projection that respects the authored aspect + sensor fit.
    }

    if (znear > 0.0f)
        near_plane = znear;

    if (zfar > 0.0f)
        far_plane = zfar;
    else
        far_plane = 100000.0f;   // glTF zfar == 0 means "infinite"
}

void Camera::save_initial_pose() {
    initial_position_ = position;
    initial_orientation_ = orientation;
    initial_fov_degrees_ = fov_degrees;
    initial_near_plane_ = near_plane;
    initial_far_plane_ = far_plane;
    has_initial_pose_ = true;
}

void Camera::restore_initial_pose() {
    if (!has_initial_pose_) {
        return;
    }
    position = initial_position_;
    orientation = initial_orientation_;
    fov_degrees = initial_fov_degrees_;
    near_plane = initial_near_plane_;
    far_plane = initial_far_plane_;

    // Re-derive legacy yaw/pitch from the restored orientation (best effort; quat is authoritative)
    glm::vec3 dir = get_forward();
    yaw = glm::degrees(std::atan2(dir.z, dir.x));
    pitch = glm::degrees(std::asin(dir.y));
}

} // namespace scene
