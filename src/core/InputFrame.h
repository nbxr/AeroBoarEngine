#pragma once

#include "core/InputManager.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

namespace core {

// Snapshot of device input for one frame. Built after InputManager::update.
// Systems consume this — not raw InputManager key branching in main/camera.
struct InputFrame {
    float delta_time = 0.0f;
    glm::vec2 mouse_delta{0.0f};
    bool cursor_captured = false;

    // Held (true while key is down this frame)
    bool w = false;
    bool a = false;
    bool s = false;
    bool d = false;
    bool space = false;
    bool left_shift = false;
    bool right_shift = false;
    bool q = false;
    bool e = false;

    // Rising edges (true only on the frame the key was pressed)
    bool escape_pressed = false;
    bool r_pressed = false;
    bool p_pressed = false;
    bool n_pressed = false;
    bool y_pressed = false;
    bool t_pressed = false;
    bool f3_pressed = false; // physics debug draw toggle

    [[nodiscard]] bool shift_held() const {
        return left_shift || right_shift;
    }
};

// Tracks previous key state so rising edges are available without statics in main.
class InputFrameBuilder {
  public:
    InputFrame build(const InputManager& input, float delta_time) {
        InputFrame f{};
        f.delta_time = delta_time;
        f.mouse_delta = input.get_mouse_delta();
        f.cursor_captured = input.is_cursor_captured();

        f.w = input.is_key_down(GLFW_KEY_W);
        f.a = input.is_key_down(GLFW_KEY_A);
        f.s = input.is_key_down(GLFW_KEY_S);
        f.d = input.is_key_down(GLFW_KEY_D);
        f.space = input.is_key_down(GLFW_KEY_SPACE);
        f.left_shift = input.is_key_down(GLFW_KEY_LEFT_SHIFT);
        f.right_shift = input.is_key_down(GLFW_KEY_RIGHT_SHIFT);
        f.q = input.is_key_down(GLFW_KEY_Q);
        f.e = input.is_key_down(GLFW_KEY_E);

        const bool escape = input.is_key_down(GLFW_KEY_ESCAPE);
        const bool r = input.is_key_down(GLFW_KEY_R);
        const bool p = input.is_key_down(GLFW_KEY_P);
        const bool n = input.is_key_down(GLFW_KEY_N);
        const bool y = input.is_key_down(GLFW_KEY_Y);
        const bool t = input.is_key_down(GLFW_KEY_T);
        const bool f3 = input.is_key_down(GLFW_KEY_F3);

        f.escape_pressed = edge(escape, prev_escape_);
        f.r_pressed = edge(r, prev_r_);
        f.p_pressed = edge(p, prev_p_);
        f.n_pressed = edge(n, prev_n_);
        f.y_pressed = edge(y, prev_y_);
        f.t_pressed = edge(t, prev_t_);
        f.f3_pressed = edge(f3, prev_f3_);

        prev_escape_ = escape;
        prev_r_ = r;
        prev_p_ = p;
        prev_n_ = n;
        prev_y_ = y;
        prev_t_ = t;
        prev_f3_ = f3;
        return f;
    }

    void reset_edges() {
        prev_escape_ = prev_r_ = prev_p_ = prev_n_ = prev_y_ = prev_t_ =
            prev_f3_ = false;
    }

  private:
    static bool edge(bool now, bool prev) { return now && !prev; }

    bool prev_escape_ = false;
    bool prev_r_ = false;
    bool prev_p_ = false;
    bool prev_n_ = false;
    bool prev_y_ = false;
    bool prev_t_ = false;
    bool prev_f3_ = false;
};

} // namespace core
