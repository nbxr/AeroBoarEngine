#pragma once

// GLM configuration for Vulkan (0..1 depth range instead of OpenGL's -1..1)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declarations to avoid pulling GLFW into the public Camera header
struct GLFWwindow;

namespace core {
class InputManager;
}

namespace scene {

enum class CameraMode {
    Desktop,   // WASD + mouse (development)
    VR         // Driven by OpenXR head tracking (future)
};

/**
 * @brief 6DOF-style desktop development camera with quaternion-based orientation.
 *
 * Controls (Desktop mode):
 * - WASD: Move forward/back/strafe left/right relative to current orientation.
 * - Space / Left-Shift: Move up/down along the camera's local up vector.
 * - Mouse (when captured): 
 *     - X: Yaw (rotate left/right) around the camera's current Up vector.
 *     - Y: Pitch up/down (direction controlled by `invert_pitch`).
 * - Q / E: Roll the camera counterclockwise / clockwise around its forward axis.
 * - R: Restore the exact camera pose from when the scene was loaded (authored
 *   glTF camera node if present, otherwise the initial AABB framing). This is the
 *   "get me back to the starting view" action.
 * - Shift+R: Perform a generic AABB-based "frame the model nicely" (old R behavior).
 *
 * Startup behavior: The app starts with the cursor visible (mouse capture off).
 * Move the mouse over the window freely (no camera effect). Press Escape to
 * toggle capture on and start mouse-look control. Escape always toggles.
 * - Escape: Toggle mouse capture (robust jump prevention is handled by InputManager).
 *
 * Public tunables:
 * - `movement_speed` (WASD / Space / Shift; runtime: **Y** faster, **T** slower)
 * - `mouse_sensitivity` (mouse look only; not affected by Y/T)
 * - `roll_speed` (Q/E roll rate in degrees per second)
 * - `invert_pitch` (default false = normal behavior)
 * - `fov_degrees`, `near_plane`, `far_plane`
 *
 * The camera maintains a quaternion `orientation` internally for robust 6DOF
 * rotation (including roll). `get_view_matrix()` is derived from the current
 * position + orientation.
 *
 * Note: The user may have applied personal sign tweaks inside the implementation
 * to match their preferred feel. The public API and documented behavior above
 * should be treated as the intended interface.
 */
class Camera {
public:
    explicit Camera(GLFWwindow* glfwWindow = nullptr);

    void set_mode(CameraMode new_mode);
    [[nodiscard]] CameraMode get_mode() const { return mode; }

    // Main update - call once per frame
    // input: provides processed keyboard + smoothed mouse deltas (and capture state)
    void update(float delta_time, core::InputManager& input);

    // TODO(desktop-input): Consider exposing runtime tuning for smoothing/acceleration
    // (currently only available via InputManager setters) or a small debug UI.

    [[nodiscard]] glm::mat4 get_view_matrix() const;
    [[nodiscard]] glm::mat4 get_projection_matrix(float aspect_ratio) const;

    // === VR support (stub for now) ===
    void set_vr_view_matrix(const glm::mat4& view_matrix);

    // === Configuration / Queries ===
    void set_position(const glm::vec3& position);
    [[nodiscard]] glm::vec3 get_position() const { return position; }

    // Returns the world-space direction the camera is facing (our "front").
    // Equivalent to normalize(orientation * vec3(0,0,-1)).
    [[nodiscard]] glm::vec3 get_forward() const;

    // Frames the camera to nicely view a sphere (center + radius)
    void frame(const glm::vec3& center, float radius);

    // Set world position and look direction (forward = direction the camera faces).
    // Used by configuration cameraOverride and debug tools.
    void set_position_and_forward(const glm::vec3& pos, const glm::vec3& forward);

    // Minimal support: apply a glTF camera node's world transform + projection params.
    // aspect_ratio (if > 0) comes from camera.perspective.aspectRatio in the glTF.
    // When 0 we keep using the runtime window aspect in get_projection_matrix().
    void set_from_camera_node(const glm::mat4& world_transform,
                              float yfov_radians,
                              float znear,
                              float zfar,
                              float aspect_ratio = 0.0f);

    // Resets internal mouse tracking state. Call this after toggling cursor capture
    // (e.g. with Escape) so that the next mouse delta does not cause a large jump.
    void reset_mouse_state();

    // Save/restore the camera pose that was active immediately after the scene finished loading.
    // This is used by the 'R' key so that it returns to the authored (glTF camera node) or
    // initial framed view instead of always computing a generic AABB offset.
    void save_initial_pose();
    void restore_initial_pose();

    // Tunables
    float movement_speed = 5.0f;
    float mouse_sensitivity = 0.35f;
    float roll_speed = 90.0f;     // Q/E roll rate in degrees per second
    float fov_degrees = 60.0f;
    // Defaults suit small glTF assets (e.g. ABeautifulGame). frame() retunes
    // near/far from the framing sphere so large scenes stay stable too.
    float near_plane = 0.01f;
    float far_plane = 1000.0f;

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

    // VR override
    glm::mat4 vr_view_matrix{1.0f};

    void update_desktop(float delta_time, core::InputManager& input);

    // Saved pose from right after load (for 'R' reset to "where it was when loaded")
    bool has_initial_pose_ = false;
    glm::vec3 initial_position_{0.0f};
    glm::quat initial_orientation_{1.0f, 0.0f, 0.0f, 0.0f};
    float initial_fov_degrees_ = 60.0f;
    float initial_near_plane_ = 0.01f;
    float initial_far_plane_ = 1000.0f;
};

} // namespace scene
