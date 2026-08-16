#include "core/InputManager.h"
#include "core/Log.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <glm/gtc/epsilon.hpp>

namespace core {
namespace {

bool detect_remote_session() {
#ifdef _WIN32
    if (GetSystemMetrics(SM_REMOTESESSION) != 0)
        return true;
    if (const char* session = std::getenv("SESSIONNAME")) {
        // Typical: "RDP-Tcp#0". Console is local.
        if (std::strncmp(session, "RDP-", 4) == 0)
            return true;
    }
#endif
    // xRDP / FreeRDP-style sessions (Linux). Not SSH_CONNECTION — that is
    // X11-forward / ssh and is not this mouse-path problem.
    static const char* kRemoteEnv[] = {"XRDP_SESSION", "XRDP_SOCKET_PATH",
                                       "XRDP_SOCKET_IN_PORT", "RDP_SESSION"};
    for (const char* key : kRemoteEnv) {
        const char* val = std::getenv(key);
        if (val && val[0] != '\0')
            return true;
    }
    return false;
}

constexpr int kRemoteSettleCallbacks = 4;

} // namespace

InputManager &InputManager::get_instance() {
    static InputManager instance;
    return instance;
}

void InputManager::initialize(GLFWwindow *window) {
    window_ = window;
    glfwSetWindowUserPointer(window_, this);

    glfwSetCursorPosCallback(window_, cursor_position_callback);
    glfwSetKeyCallback(window_, key_callback);
    glfwSetMouseButtonCallback(window_, mouse_button_callback);

    remote_session_ = detect_remote_session();
#if AERO_DEBUG_FORCE_NORMAL_CURSOR
    LOG_INFO("[Input] remote_session=" << (remote_session_ ? "yes" : "no")
             << " AERO_DEBUG_FORCE_NORMAL_CURSOR=1 (HIDDEN instead of DISABLED)");
#else
    LOG_INFO("[Input] remote_session=" << (remote_session_ ? "yes" : "no"));
#endif

    // Initialize mouse position and tracking baseline
    double x, y;
    glfwGetCursorPos(window_, &x, &y);
    mouse_position_ = glm::vec2(static_cast<float>(x), static_cast<float>(y));
    last_mouse_position_ = mouse_position_;
    raw_mouse_delta_ = glm::vec2(0.0f);
    smoothed_mouse_delta_ = glm::vec2(0.0f);
}

void InputManager::update(float delta_time) {
    if (!window_)
        return;

    // 1. Mouse acceleration (non-linear boost for fast movements while
    // preserving precision for slow ones) Formula matches the original
    // implementation plan: acceleration = 1.0 + (magnitude * scale)
    float mag = glm::length(raw_mouse_delta_);
    float acceleration = 1.0f + (mag * acceleration_scale);
    glm::vec2 accelerated_delta = raw_mouse_delta_ * acceleration;

    // 2. Temporal smoothing via Exponentially Weighted Moving Average (EWMA)
    // smoothed = alpha * new + (1 - alpha) * previous_smoothed
    // Current defaults (alpha=0.75 in header, scale=0.008) were tuned for comfortable
    // desktop inspection feel.
    smoothed_mouse_delta_ = smoothing_alpha * accelerated_delta +
                            (1.0f - smoothing_alpha) * smoothed_mouse_delta_;

    // While the suppress flag is still set (no captured cursor callback has run
    // since last reset/toggle), force clean zero smoothed. This prevents any
    // stale or warp delta from the previous state from affecting look right
    // after capturing with Escape.
    // We expire the flag here (if no cb arrived yet) so the *next* cb's delta
    // will be taken (from the reset baseline).
    if (suppress_next_mouse_delta_) {
        smoothed_mouse_delta_ = glm::vec2(0.0f);
        suppress_next_mouse_delta_ = false;
    }

    // 3. Reset raw accumulator for the next frame
    raw_mouse_delta_ = glm::vec2(0.0f);

    // TODO(desktop-input): Consider time-constant smoothing using delta_time
    // (e.g. effective_alpha = 1 - pow(1 - alpha, dt * 60)) when frame rate
    // varies significantly (e.g. 30Hz vs 144Hz+). Current per-frame alpha is
    // acceptable for typical fixed desktop refresh rates.
}

bool InputManager::is_key_down(int key) const {
    if (key < 0 || key >= GLFW_KEY_LAST)
        return false;
    return keys_[key];
}

glm::vec2 InputManager::get_mouse_delta() const {
    return smoothed_mouse_delta_;
}

glm::vec2 InputManager::get_mouse_position() const { return mouse_position_; }

bool InputManager::is_mouse_button_down(int button) const {
    if (button < 0 || button >= GLFW_MOUSE_BUTTON_LAST)
        return false;
    return mouse_buttons_[button];
}

// --- Capture control ---

void InputManager::set_cursor_captured(bool captured) {
    if (!window_)
        return;

    // Capture flag still follows the caller (Escape). GLFW mode:
    //   local     → DISABLED (relative / infinite look)
    //   remote    → HIDDEN   (RDP absolute coords + DISABLED stalls)
    //   diagnostic → HIDDEN even locally
    int mode = GLFW_CURSOR_NORMAL;
    if (captured) {
#if AERO_DEBUG_FORCE_NORMAL_CURSOR
        mode = GLFW_CURSOR_HIDDEN;
#else
        mode = remote_session_ ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_DISABLED;
#endif
    }
    glfwSetInputMode(window_, GLFW_CURSOR, mode);
    cursor_captured_ = captured;
    remote_settle_remaining_ =
        (captured && remote_session_) ? kRemoteSettleCallbacks : 0;
    reset_mouse_state();  // centralizes baseline snapshot + suppress for jump-free toggle
}

void InputManager::reset_mouse_state() {
    if (!window_)
        return;

    // Snapshot current position as new baseline and clear pending deltas.
    // Sets the suppress flag so the next update forces a clean zero delta
    // (until the first captured cb or the update itself expires it).
    // Combined with the per-cb large-delta guard, this prevents jumps on
    // Escape toggles while still allowing immediate small user deltas after
    // capture (the first post-reset cb delta is taken unless it is itself large).
    double x, y;
    glfwGetCursorPos(window_, &x, &y);
    mouse_position_ = glm::vec2(static_cast<float>(x), static_cast<float>(y));
    last_mouse_position_ = mouse_position_;
    raw_mouse_delta_ = glm::vec2(0.0f);
    smoothed_mouse_delta_ = glm::vec2(0.0f);
    suppress_next_mouse_delta_ = true;
}

// --- Callbacks ---

void InputManager::cursor_position_callback(GLFWwindow *window, double xpos,
                                            double ypos) {
    auto *self = static_cast<InputManager *>(glfwGetWindowUserPointer(window));
    if (!self)
        return;

    glm::vec2 new_pos(static_cast<float>(xpos), static_cast<float>(ypos));
    self->mouse_position_ = new_pos;

    glm::vec2 delta = new_pos - self->last_mouse_position_;
    self->last_mouse_position_ = new_pos;

    if (self->cursor_captured_) {
        // Any captured cb "satisfies" a pending suppress (so update will stop
        // forcing zero). Independently, swallow this delta only if it is
        // suspiciously large (warp on toggle, or other OS jump). Small deltas --
        // including the very first user move cb after an Escape capture -- are
        // always accumulated (relative to the baseline set in reset_mouse_state).
        // This ensures mouse look "updates" (responds) immediately after capture.
        self->suppress_next_mouse_delta_ = false;

        const float mag = glm::length(delta);
#if AERO_DEBUG_FORCE_NORMAL_CURSOR
        LOG_INFO("[Input] captured raw |delta|=" << mag << " d=(" << delta.x
                 << ", " << delta.y << ")");
#endif

        if (self->remote_session_ && self->remote_settle_remaining_ > 0) {
            --self->remote_settle_remaining_;
            return;
        }

        // Local: 1000px warp guard (unchanged). Remote: RDP often reports
        // absolute coords as 100–800px "moves" — treat those as reposition.
#if AERO_DEBUG_FORCE_NORMAL_CURSOR
        const float thresh = 1.0e6f; // see raw numbers; do not swallow
#else
        const float thresh = self->remote_session_
                                 ? self->remote_warp_threshold_
                                 : self->mouse_delta_threshold_;
#endif
        if (mag > thresh) {
            // Absolute reposition: last_pos already updated; do not look.
            return;
        }

        self->raw_mouse_delta_ += delta;
    }
    // When not captured we still keep last_pos / mouse_position_ fresh so that
    // the next time we capture the baseline is good.
}

void InputManager::key_callback(GLFWwindow *window, int key, int scancode,
                                int action, int mods) {
    auto *self = static_cast<InputManager *>(glfwGetWindowUserPointer(window));
    if (!self || key < 0 || key >= GLFW_KEY_LAST)
        return;

    if (action == GLFW_PRESS) {
        self->keys_[key] = true;
    } else if (action == GLFW_RELEASE) {
        self->keys_[key] = false;
    }
    // Note: GLFW_REPEAT is ignored for is_key_down state (fine for our use).
}

void InputManager::mouse_button_callback(GLFWwindow *window, int button,
                                         int action, int mods) {
    auto *self = static_cast<InputManager *>(glfwGetWindowUserPointer(window));
    if (!self || button < 0 || button >= GLFW_MOUSE_BUTTON_LAST)
        return;

    if (action == GLFW_PRESS) {
        self->mouse_buttons_[button] = true;
    } else if (action == GLFW_RELEASE) {
        self->mouse_buttons_[button] = false;
    }
}

} // namespace core
