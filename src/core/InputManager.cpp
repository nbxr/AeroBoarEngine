#include "core/InputManager.h"

#include <glm/gtc/epsilon.hpp>
#include <algorithm>
#include <cmath>

namespace core {

InputManager& InputManager::get_instance() {
    static InputManager instance;
    return instance;
}

void InputManager::initialize(GLFWwindow* window) {
    window_ = window;
    glfwSetWindowUserPointer(window_, this);

    glfwSetCursorPosCallback(window_, cursor_position_callback);
    glfwSetKeyCallback(window_, key_callback);
    glfwSetMouseButtonCallback(window_, mouse_button_callback);

    // Initialize mouse position and tracking baseline
    double x, y;
    glfwGetCursorPos(window_, &x, &y);
    mouse_position_ = glm::vec2(static_cast<float>(x), static_cast<float>(y));
    last_mouse_position_ = mouse_position_;
    raw_mouse_delta_ = glm::vec2(0.0f);
    smoothed_mouse_delta_ = glm::vec2(0.0f);
}

void InputManager::update(float delta_time) {
    if (!window_) return;

    // 1. Mouse acceleration (non-linear boost for fast movements while preserving precision for slow ones)
    // Formula matches the original implementation plan: acceleration = 1.0 + (magnitude * scale)
    float mag = glm::length(raw_mouse_delta_);
    float acceleration = 1.0f + (mag * acceleration_scale);
    glm::vec2 accelerated_delta = raw_mouse_delta_ * acceleration;

    // 2. Temporal smoothing via Exponentially Weighted Moving Average (EWMA)
    // smoothed = alpha * new + (1 - alpha) * previous_smoothed
    // Current defaults (alpha=0.35, scale=0.008) were tuned for comfortable desktop inspection feel.
    smoothed_mouse_delta_ = smoothing_alpha * accelerated_delta +
                            (1.0f - smoothing_alpha) * smoothed_mouse_delta_;

    // 3. Reset raw accumulator for the next frame
    raw_mouse_delta_ = glm::vec2(0.0f);

    // TODO(desktop-input): Consider time-constant smoothing using delta_time
    // (e.g. effective_alpha = 1 - pow(1 - alpha, dt * 60)) when frame rate
    // varies significantly (e.g. 30Hz vs 144Hz+). Current per-frame alpha is
    // acceptable for typical fixed desktop refresh rates.
}

bool InputManager::is_key_down(int key) const {
    if (key < 0 || key >= GLFW_KEY_LAST) return false;
    return keys_[key];
}

glm::vec2 InputManager::get_mouse_delta() const {
    return smoothed_mouse_delta_;
}

glm::vec2 InputManager::get_mouse_position() const {
    return mouse_position_;
}

bool InputManager::is_mouse_button_down(int button) const {
    if (button < 0 || button >= GLFW_MOUSE_BUTTON_LAST) return false;
    return mouse_buttons_[button];
}

// --- Capture control ---

void InputManager::set_cursor_captured(bool captured) {
    if (!window_) return;

    glfwSetInputMode(window_, GLFW_CURSOR,
                     captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    cursor_captured_ = captured;
    reset_mouse_state();
}

void InputManager::reset_mouse_state() {
    if (!window_) return;

    // Snapshot current position as new baseline and clear pending deltas.
    // This prevents large spurious deltas when GLFW/OS warps the cursor
    // during mode transitions (Escape toggle, etc.).
    double x, y;
    glfwGetCursorPos(window_, &x, &y);
    mouse_position_ = glm::vec2(static_cast<float>(x), static_cast<float>(y));
    last_mouse_position_ = mouse_position_;
    raw_mouse_delta_ = glm::vec2(0.0f);
    smoothed_mouse_delta_ = glm::vec2(0.0f);
}

// --- Callbacks ---

void InputManager::cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!self) return;

    glm::vec2 new_pos(static_cast<float>(xpos), static_cast<float>(ypos));
    self->mouse_position_ = new_pos;

    // Accumulate raw delta (important for sub-frame precision across multiple
    // callbacks between frames). We always track; gating of look happens at
    // consumption time in Camera.
    self->raw_mouse_delta_ += (new_pos - self->last_mouse_position_);
    self->last_mouse_position_ = new_pos;
}

void InputManager::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!self || key < 0 || key >= GLFW_KEY_LAST) return;

    if (action == GLFW_PRESS) {
        self->keys_[key] = true;
    } else if (action == GLFW_RELEASE) {
        self->keys_[key] = false;
    }
    // Note: GLFW_REPEAT is ignored for is_key_down state (fine for our use).
}

void InputManager::mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    auto* self = static_cast<InputManager*>(glfwGetWindowUserPointer(window));
    if (!self || button < 0 || button >= GLFW_MOUSE_BUTTON_LAST) return;

    if (action == GLFW_PRESS) {
        self->mouse_buttons_[button] = true;
    } else if (action == GLFW_RELEASE) {
        self->mouse_buttons_[button] = false;
    }
}

} // namespace core
