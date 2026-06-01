#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace core {

/**
 * @brief GLFW-based input manager for desktop development (singleton for callback compatibility).
 *
 * Responsibilities:
 * - High-precision mouse delta accumulation via cursor callback (sub-frame safe).
 * - Per-frame processing: acceleration + EWMA smoothing.
 * - Keyboard and mouse button state queries.
 * - Cursor capture state (owns glfwSetInputMode + anti-jump reset logic).
 *
 * Consumers (Camera, main loop) query processed state. Sensitivity/invert_pitch
 * are view concerns and live in scene::Camera (applied on top of get_mouse_delta()).
 *
 * TODO(desktop-input): Consider making this Engine-owned with a thin static
 * trampoline if we ever need multiple contexts or easier testing.
 */
class InputManager {
public:
    static InputManager& get_instance();

    // Disable copy and assignment
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    void initialize(GLFWwindow* window);
    void update(float delta_time);

    // Keyboard queries
    bool is_key_down(int key) const;

    // Mouse queries
    glm::vec2 get_mouse_delta() const;
    glm::vec2 get_mouse_position() const;
    bool is_mouse_button_down(int button) const;

    // Capture control (centralized to prevent delta jumps on toggle)
    void set_cursor_captured(bool captured);
    bool is_cursor_captured() const { return cursor_captured_; }
    void reset_mouse_state();

    // Low-level processing tunables (smoothing/accel only; higher-level view
    // tunables like sensitivity live in Camera)
    void set_smoothing_alpha(float alpha) { smoothing_alpha = alpha; }
    void set_acceleration_scale(float scale) { acceleration_scale = scale; }

private:
    InputManager() = default;
    ~InputManager() = default;

    // GLFW Callbacks
    static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);

    GLFWwindow* window_{nullptr};

    // Key State
    bool keys_[GLFW_KEY_LAST] = {false};

    // Mouse State
    bool mouse_buttons_[GLFW_MOUSE_BUTTON_LAST] = {false};
    glm::vec2 mouse_position_{0.0f};
    glm::vec2 last_mouse_position_{0.0f};
    glm::vec2 raw_mouse_delta_{0.0f};
    glm::vec2 smoothed_mouse_delta_{0.0f};

    // Capture state (owned here so reset logic is centralized)
    bool cursor_captured_ = false;

    // Configuration (processing only)
    // These defaults were chosen empirically for comfortable desktop model/scene
    // inspection on Linux (responsive without feeling laggy or jittery).
    // They can be adjusted at runtime via the setters for different mice / DPI / preference.
    float smoothing_alpha = 0.35f;     // EWMA smoothing factor (higher = more lag, more stable)
    float acceleration_scale = 0.008f; // Controls non-linear boost: acceleration = 1.0 + (mag * scale)
};

} // namespace core
