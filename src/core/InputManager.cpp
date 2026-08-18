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
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
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

#ifdef _WIN32
HWND g_hwnd = nullptr;
WNDPROC g_prev_wndproc = nullptr;
bool g_raw_have_abs = false;
LONG g_raw_last_ax = 0;
LONG g_raw_last_ay = 0;
// Set when we warp the OS cursor. The next *near-center* absolute sample is
// that warp (new baseline, not look). A sample that is not near center is
// treated as the user — do not drop it.
bool g_raw_expect_warp = false;
double g_raw_warp_time = 0.0;
double g_raw_warp_attempt_time = -1.0;
bool g_raw_on_rail = false;
bool g_logged_warp_fail = false;

constexpr float kAbsJumpPx = 1000.0f;
constexpr float kWarpNearCenterPx = 48.0f;
constexpr LONG kAbsRailUnits = 512; // ~0.8% of 0..65535
constexpr double kWarpSnapbackSec = 0.08;

bool client_center_screen(POINT* out) {
    if (!g_hwnd || !out)
        return false;
    RECT rc{};
    if (!GetClientRect(g_hwnd, &rc))
        return false;
    POINT c{(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
    if (!ClientToScreen(g_hwnd, &c))
        return false;
    *out = c;
    return true;
}

bool near_virtual_desktop_edge(int margin_px) {
    POINT p{};
    if (!GetCursorPos(&p))
        return false;
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return p.x <= vx + margin_px || p.x >= vx + vw - 1 - margin_px ||
           p.y <= vy + margin_px || p.y >= vy + vh - 1 - margin_px;
}

bool sync_cursor_client_pos(glm::vec2* out) {
    if (!g_hwnd || !out)
        return false;
    POINT p{};
    if (!GetCursorPos(&p))
        return false;
    if (!ScreenToClient(g_hwnd, &p))
        return false;
    *out = glm::vec2(static_cast<float>(p.x), static_cast<float>(p.y));
    return true;
}

void abs_sample_screen_px(const RAWMOUSE& m, float* sx, float* sy) {
    if (m.usFlags & MOUSE_VIRTUAL_DESKTOP) {
        const float vw = static_cast<float>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
        const float vh = static_cast<float>(GetSystemMetrics(SM_CYVIRTUALSCREEN));
        *sx = static_cast<float>(m.lLastX) * (vw / 65535.0f) +
              static_cast<float>(GetSystemMetrics(SM_XVIRTUALSCREEN));
        *sy = static_cast<float>(m.lLastY) * (vh / 65535.0f) +
              static_cast<float>(GetSystemMetrics(SM_YVIRTUALSCREEN));
    } else {
        const float sw = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
        const float sh = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
        *sx = static_cast<float>(m.lLastX) * (sw / 65535.0f);
        *sy = static_cast<float>(m.lLastY) * (sh / 65535.0f);
    }
}

LRESULT CALLBACK remote_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_INPUT) {
        InputManager& self = InputManager::get_instance();
        if (self.is_cursor_captured() && self.uses_hidden_capture()) {
            UINT size = sizeof(RAWINPUT);
            RAWINPUT raw{};
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw,
                                &size, sizeof(RAWINPUTHEADER)) >= sizeof(RAWINPUTHEADER) &&
                raw.header.dwType == RIM_TYPEMOUSE) {
                const RAWMOUSE& m = raw.data.mouse;
                if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {
                    int sw = GetSystemMetrics(SM_CXSCREEN);
                    int sh = GetSystemMetrics(SM_CYSCREEN);
                    if (m.usFlags & MOUSE_VIRTUAL_DESKTOP) {
                        sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                        sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                    }

                    bool consumed_warp = false;
                    if (g_raw_expect_warp) {
                        g_raw_expect_warp = false;
                        POINT center{};
                        float sx = 0.0f;
                        float sy = 0.0f;
                        abs_sample_screen_px(m, &sx, &sy);
                        if (client_center_screen(&center) &&
                            std::fabs(sx - static_cast<float>(center.x)) < kWarpNearCenterPx &&
                            std::fabs(sy - static_cast<float>(center.y)) < kWarpNearCenterPx) {
                            consumed_warp = true;
                        }
                    }

                    if (!consumed_warp && g_raw_have_abs) {
                        const float dx = static_cast<float>(m.lLastX - g_raw_last_ax) *
                                         (static_cast<float>(sw) / 65535.0f);
                        const float dy = static_cast<float>(m.lLastY - g_raw_last_ay) *
                                         (static_cast<float>(sh) / 65535.0f);
                        const float adx = std::fabs(dx);
                        const float ady = std::fabs(dy);
                        // RDP often snaps the cursor back to the client position
                        // right after a server-side SetCursorPos. That is not look.
                        const bool snapback =
                            (glfwGetTime() - g_raw_warp_time) < kWarpSnapbackSec &&
                            (adx > 48.0f || ady > 48.0f);
                        if (!snapback && adx < kAbsJumpPx && ady < kAbsJumpPx)
                            self.add_captured_look_delta(glm::vec2(dx, dy));
                    }
                    g_raw_last_ax = m.lLastX;
                    g_raw_last_ay = m.lLastY;
                    g_raw_have_abs = true;
                    g_raw_on_rail = (m.lLastX < kAbsRailUnits ||
                                     m.lLastX > 65535 - kAbsRailUnits ||
                                     m.lLastY < kAbsRailUnits ||
                                     m.lLastY > 65535 - kAbsRailUnits);
                } else if (m.lLastX != 0 || m.lLastY != 0) {
                    self.add_captured_look_delta(glm::vec2(
                        static_cast<float>(m.lLastX), static_cast<float>(m.lLastY)));
                    g_raw_on_rail = false;
                }
            }
        }
    }
    return CallWindowProc(g_prev_wndproc, hwnd, msg, wparam, lparam);
}

void install_windows_raw_look(GLFWwindow* window, bool* out_ok) {
    *out_ok = false;
    g_hwnd = glfwGetWin32Window(window);
    if (!g_hwnd)
        return;
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = 0;
    rid.hwndTarget = g_hwnd;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        LOG_ERROR("[Input] RegisterRawInputDevices failed");
        return;
    }
    g_prev_wndproc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(remote_wnd_proc)));
    *out_ok = g_prev_wndproc != nullptr;
}
#endif

} // namespace

bool InputManager::uses_hidden_capture() const {
#if AERO_DEBUG_FORCE_NORMAL_CURSOR
    return true;
#else
    return remote_session_;
#endif
}

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
#ifdef _WIN32
    if (uses_hidden_capture()) {
        install_windows_raw_look(window_, &raw_look_active_);
        LOG_INFO("[Input] remote_session=" << (remote_session_ ? "yes" : "no")
                 << " windows_raw_look=" << (raw_look_active_ ? "yes" : "no"));
    } else
#endif
    {
        LOG_INFO("[Input] remote_session=" << (remote_session_ ? "yes" : "no"));
    }

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

    if (cursor_captured_ && uses_hidden_capture()) {
#ifdef _WIN32
        // Keep mouse_position_ honest (RDP SetCursorPos is often ignored).
        glm::vec2 os_pos{};
        if (sync_cursor_client_pos(&os_pos)) {
            mouse_position_ = os_pos;
            last_mouse_position_ = os_pos;
        }
#endif
        // Do not ClipCursor to the window — that is the "stops at a point"
        // cage. WM_INPUT still arrives while we have focus. Only try to warp
        // when the OS pointer is on the desktop rail (or absolute 0/65535).
        recenter_hidden_cursor(/*force=*/false);
    }

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
    if (captured)
        mode = uses_hidden_capture() ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_DISABLED;
    glfwSetInputMode(window_, GLFW_CURSOR, mode);
    cursor_captured_ = captured;
    remote_settle_remaining_ = (captured && uses_hidden_capture()) ? 1 : 0;
    reset_mouse_state();
#ifdef _WIN32
    g_raw_have_abs = false;
    g_raw_expect_warp = false;
    g_raw_on_rail = false;
    g_raw_warp_attempt_time = -1.0;
    ClipCursor(nullptr);
#endif
    if (captured && uses_hidden_capture())
        recenter_hidden_cursor(/*force=*/true);
}

void InputManager::add_captured_look_delta(const glm::vec2& delta) {
    if (!cursor_captured_)
        return;
    raw_mouse_delta_ += delta;
}

void InputManager::recenter_hidden_cursor(bool force) {
    if (!window_)
        return;
    int w = 0, h = 0;
    glfwGetWindowSize(window_, &w, &h);
    if (w < 2 || h < 2)
        return;
    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);

#ifdef _WIN32
    // Window edges are not a stop. Only the virtual-desktop rail (and the
    // 0/65535 absolute rail) needs a warp attempt. ClipCursor-to-window
    // plus "recenter at 15% of a 640x480 client" is what made look die
    // after one short trackpad stroke.
    if (!force && !g_raw_on_rail && !near_virtual_desktop_edge(32))
        return;
    // RDP often ignores SetCursorPos. Do not spam warps every frame on the rail.
    if (!force && g_raw_warp_attempt_time >= 0.0 &&
        (glfwGetTime() - g_raw_warp_attempt_time) < 0.25)
        return;
    g_raw_warp_attempt_time = glfwGetTime();
#else
    const float margin = std::max(48.0f, std::min(fw, fh) * 0.15f);
    const bool near_edge =
        mouse_position_.x < margin || mouse_position_.x > fw - margin ||
        mouse_position_.y < margin || mouse_position_.y > fh - margin;
    if (!force && !near_edge)
        return;
#endif

    const float cx = fw * 0.5f;
    const float cy = fh * 0.5f;
    pending_recenter_ = true;
    recenter_target_ = glm::vec2(cx, cy);
#ifdef _WIN32
    POINT target{};
    if (client_center_screen(&target)) {
        SetCursorPos(target.x, target.y);
        SetPhysicalCursorPos(target.x, target.y);
    }
#endif
    glfwSetCursorPos(window_, static_cast<double>(cx), static_cast<double>(cy));
#ifdef _WIN32
    POINT now{};
    const bool honored = GetCursorPos(&now) && client_center_screen(&target) &&
                         std::abs(now.x - target.x) < 8 &&
                         std::abs(now.y - target.y) < 8;
    if (honored) {
        mouse_position_ = last_mouse_position_ = recenter_target_;
        g_raw_expect_warp = true;
        g_raw_warp_time = glfwGetTime();
        g_raw_on_rail = false;
    } else {
        // Typical RDP: the client pointer does not move. Do not pretend we
        // are centered — that stopped further warp attempts and look died
        // at the window/screen edge. Leave the OS position as-is.
        glm::vec2 os_pos{};
        if (sync_cursor_client_pos(&os_pos)) {
            mouse_position_ = last_mouse_position_ = os_pos;
        }
        pending_recenter_ = false;
        if (!g_logged_warp_fail) {
            g_logged_warp_fail = true;
            LOG_INFO("[Input] cursor warp not honored (typical RDP). "
                     "Look uses the full desktop until the screen edge.");
        }
    }
#else
    mouse_position_ = last_mouse_position_ = recenter_target_;
#endif
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
    if (self->pending_recenter_) {
        const float d = glm::length(new_pos - self->recenter_target_);
        if (d < 12.0f) {
            self->pending_recenter_ = false;
            self->mouse_position_ = new_pos;
            self->last_mouse_position_ = new_pos;
            return;
        }
        // Not the warp (real user event) — consume the pending flag and apply.
        self->pending_recenter_ = false;
    }
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
        // HIDDEN path: coalesced RDP moves are real and often >96px. Only
        // swallow true warps (same 1000px guard as local).
        const float thresh = self->mouse_delta_threshold_;
#endif
        if (mag > thresh) {
            // Absolute reposition: last_pos already updated; do not look.
            return;
        }

        // Remote Windows look comes from WM_INPUT. Cursor pos is only used
        // to keep the OS pointer off the screen edge.
        if (!self->raw_look_active_)
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
