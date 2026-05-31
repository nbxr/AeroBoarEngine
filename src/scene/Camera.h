#pragma once

// GLM configuration for Vulkan (0..1 depth range instead of OpenGL's -1..1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <GLFW/glfw3.h>

namespace scene {

enum class CameraMode {
    Desktop,   // WASD + mouse (development)
    VR         // Driven by OpenXR head tracking (future)
};

class Camera {
public:
    explicit Camera(GLFWwindow* glfwWindow = nullptr);

    void set_mode(CameraMode new_mode);
    [[nodiscard]] CameraMode get_mode() const { return mode; }

    // Main update - call once per frame
    void update(float delta_time);

    [[nodiscard]] glm::mat4 get_view_matrix() const;
    [[nodiscard]] glm::mat4 get_projection_matrix(float aspect_ratio) const;

    // === VR support (stub for now) ===
    void set_vr_view_matrix(const glm::mat4& view_matrix);

    // === Configuration / Queries ===
    void set_position(const glm::vec3& position);
    [[nodiscard]] glm::vec3 get_position() const { return position; }

    // Frames the camera to nicely view a sphere (center + radius)
    void frame(const glm::vec3& center, float radius);

    // Minimal support: apply a glTF camera node's world transform + projection params
    void set_from_camera_node(const glm::mat4& world_transform,
                              float yfov_radians,
                              float znear,
                              float zfar);

    // Resets internal mouse tracking state. Call this after toggling cursor capture
    // (e.g. with Escape) so that the next mouse delta does not cause a large jump.
    void reset_mouse_state();

    // Tunables
    float movement_speed = 5.0f;
    float mouse_sensitivity = 0.35f;
    float fov_degrees = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;

    // When true, moving the mouse down will make the camera look up (inverted).
    // Default is false (normal behavior: mouse down = look down).
    bool invert_pitch = false;

private:
    CameraMode mode = CameraMode::Desktop;
    GLFWwindow* window = nullptr;

    // Transform state
    glm::vec3 position{0.0f, 1.5f, 3.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};  // w, x, y, z  (identity)

    // Legacy Euler angles (kept for frame() / set_from_camera_node() compatibility during transition)
    float yaw = -90.0f;
    float pitch = 0.0f;

    // Mouse state (desktop mode)
    double last_mouse_x = 0.0;
    double last_mouse_y = 0.0;
    bool first_mouse = true;

    // VR override
    glm::mat4 vr_view_matrix{1.0f};

    void update_desktop(float delta_time);
};

} // namespace scene
