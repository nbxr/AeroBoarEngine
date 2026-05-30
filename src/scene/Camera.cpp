#include "scene/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace scene {

Camera::Camera(GLFWwindow* glfwWindow)
    : window(glfwWindow)
{
    if (window) {
        glfwGetCursorPos(window, &last_mouse_x, &last_mouse_y);
    }

    // Better default position for typical loaded scenes (e.g. DamagedHelmet at ~origin)
    position = {0.0f, 3.0f, 10.0f};
    yaw = -90.0f;
    pitch = -10.0f;
}

void Camera::set_mode(CameraMode new_mode) {
    mode = new_mode;

    if (mode == CameraMode::Desktop && window) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        first_mouse = true;
    } else if (mode == CameraMode::Desktop && window) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void Camera::update(float delta_time) {
    if (mode == CameraMode::Desktop) {
        update_desktop(delta_time);
    }
    // VR mode: view matrix is set externally via set_vr_view_matrix()
}

void Camera::update_desktop(float delta_time) {
    if (!window) return;

    // === Keyboard movement ===
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(front);

    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

    float velocity = movement_speed * delta_time;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        position += front * velocity;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        position -= front * velocity;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        position -= right * velocity;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        position += right * velocity;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        position += up * velocity;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        position -= up * velocity;

    // === Mouse look ===
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (first_mouse) {
        last_mouse_x = xpos;
        last_mouse_y = ypos;
        first_mouse = false;
    }

    double xoffset = xpos - last_mouse_x;
    double yoffset = last_mouse_y - ypos; // reversed since y-coords go from bottom to top
    last_mouse_x = xpos;
    last_mouse_y = ypos;

    xoffset *= mouse_sensitivity;
    yoffset *= mouse_sensitivity;

    yaw   += static_cast<float>(xoffset);
    pitch += static_cast<float>(yoffset);

    // Clamp pitch
    pitch = std::clamp(pitch, -89.0f, 89.0f);
}

glm::mat4 Camera::get_view_matrix() const {
    if (mode == CameraMode::VR) {
        return vr_view_matrix;
    }

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(front);

    return glm::lookAt(position, position + front, glm::vec3(0.0f, 1.0f, 0.0f));
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

void Camera::frame(const glm::vec3& center, float radius) {
    // Nice offset angle (slightly from above and to the side)
    float distance = radius * 2.8f + 1.5f;   // some padding
    glm::vec3 offset = glm::normalize(glm::vec3(0.7f, 0.9f, 1.3f)) * distance;

    position = center + offset;

    glm::vec3 dir = glm::normalize(center - position);
    yaw = glm::degrees(std::atan2(dir.z, dir.x));
    pitch = glm::degrees(std::asin(dir.y));
}

void Camera::set_from_camera_node(const glm::mat4& world_transform,
                                  float yfov_radians,
                                  float znear,
                                  float zfar)
{
    // Position comes from the translation column of the node transform
    position = glm::vec3(world_transform[3]);

    // In glTF, the camera looks along -Z in its local space after the transform.
    // Column 2 of the matrix is the local Z axis in world space.
    glm::vec3 forward = -glm::normalize(glm::vec3(world_transform[2]));

    // Convert to our yaw/pitch convention (same math as frame())
    yaw = glm::degrees(std::atan2(forward.z, forward.x));
    pitch = glm::degrees(std::asin(forward.y));

    // Apply projection parameters from the glTF camera
    if (yfov_radians > 0.0f)
        fov_degrees = glm::degrees(yfov_radians);
    if (znear > 0.0f)
        near_plane = znear;
    if (zfar > 0.0f)
        far_plane = zfar;
}

} // namespace scene
