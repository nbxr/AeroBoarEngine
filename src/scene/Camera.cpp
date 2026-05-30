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

} // namespace scene
