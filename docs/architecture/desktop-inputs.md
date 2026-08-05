# Desktop Input Architecture

**Status**: **Completed**. Full implementation (including recovery from partial/broken prior attempt) is documented in `desktop-input-implementation.md`. The design was executed across 6 phases with verification after each.

**Scope**: Linux desktop development mode only. VR/OpenXR input is a separate future concern (driven by `scene::CameraMode::VR` and external tracking).

## Goals

- Provide high-quality, low-jitter mouse look for comfortable model/scene inspection during development.
- Decouple raw GLFW input handling (callbacks, state, event processing) from the high-level `scene::Camera` (which owns 6DOF quaternion orientation, movement math, and view/projection concerns).
- Support smooth WASD + mouse + roll controls while the cursor is captured.
- Make cursor capture toggling (Escape) robust — no spurious large mouse deltas or "jumps" on re-capture.

**Planned split (ECS near-term):** `InputManager` stays the **device** layer. Per-frame **`InputFrame`** feeds:

- **`DesktopMoveSystem`** — Player fly/look (entity with `PlayerTag` / `DesktopMove`)
- **`EditorHotkeySystem`** — Escape, R, N, P, … (**app/editor**, not on Player; Debug+Release for now)

`Camera` and `AeroBoar.cpp` stop polling keys. See `docs/architecture/ecs-plan.md` §4.4.
- Keep the CPU-side input path lightweight and cache-friendly (data-oriented style).
- Enable easy tuning of smoothing/acceleration feel without touching camera math.

## Non-Goals (Deferred)

- Full action mapping / input abstraction layer (e.g. for gameplay).
- Gamepad / joystick support.
- Raw input (evdev / HID) for absolute lowest latency (GLFW is sufficient for desktop dev).
- Multi-window or multi-device input.
- VR / OpenXR input (handled via a different path once OpenXR is integrated).
- UI / editor widget input (future concern when ImGui or equivalent is added).

## Design Overview

A single `core::InputManager` owns all GLFW input for the desktop window:

- **Why `core::`?** Input is a universal utility (not graphics-specific like `gfx::`, not game-object like `scene::`). It fits the small set of non-rendering primitives alongside `core::Handle` and `core::AABB`. It has no dependency on Vulkan, VMA, or scene data.

- **Singleton with GLFW user-pointer dispatch** (lasting architectural decision):
  - GLFW callback registration is fundamentally per-window and uses static function pointers.
  - `glfwSetWindowUserPointer` + static callbacks that cast back to the instance is the idiomatic, lightweight pattern.
  - The engine has exactly one window in desktop development mode.
  - Justification for singleton (recorded for future agents): it avoids complex ownership/lifetime issues with GLFW callbacks while still allowing explicit initialization from the application layer (`AeroBoar.cpp`). Higher-level code (`Camera`, main loop) receives an `InputManager&` rather than reaching for the global, preserving testability where it matters.
  - Future evolution path: the singleton can be replaced by an Engine-owned instance + thin static trampoline if multi-context or unit testing needs arise.

- **High-precision mouse delta via callbacks** (the core quality improvement):
  - Old approach (pre-refactor): every frame, `glfwGetCursorPos` + manual delta + `first_mouse` guard inside `Camera::update_desktop`. This is polling and can exhibit stair-stepping or frame-rate dependent behavior.
  - New approach: `glfwSetCursorPosCallback` accumulates *raw* deltas in the callback (multiple events can fire between frames). `InputManager::update(float delta_time)` (called once per frame after `glfwPollEvents`) processes the accumulated raw delta into a smoothed value.
  - This decouples sampling rate from frame rate and gives sub-frame precision.

- **Processing pipeline inside `update()`**:
  1. (Optional) Apply mouse acceleration curve to the raw delta for the frame (non-linear boost for fast movements while preserving precision for slow ones).
  2. Exponentially Weighted Moving Average (EWMA) temporal smoothing on the (accelerated) delta.
     - Formula (per the implementation plan): `smoothed = alpha * new + (1 - alpha) * previous_smoothed`.
     - `alpha` is tunable (typical 0.2–0.6 range). Note: because it is applied per frame rather than per unit time, the effective smoothing strength is frame-rate dependent. Acceptable for desktop dev (fixed refresh rates); can be made time-based later if needed.
  3. Reset the raw accumulator for the next frame.
  4. Consumers (primarily `Camera`) call `get_mouse_delta()` to obtain the processed value.

- **Keyboard state**: Simple `bool keys_[GLFW_KEY_LAST]` array updated by `glfwSetKeyCallback`. `is_key_down(int key)` for query. Edge detection (press/release this frame) remains the caller's responsibility (simple static "was_pressed" flags in the main loop are sufficient for Escape/R today).

- **Mouse button state**: Tracked for future use (buttons are cheap); not consumed by `Camera` in the initial implementation.

- **Cursor capture state machine** (owned by `InputManager`):
  - `set_cursor_captured(bool)` performs the `glfwSetInputMode` call, updates internal flag, and **immediately** resets internal mouse tracking state (last position snapshot + zero pending raw/smoothed deltas).
  - This centralizes the "prevent jump on toggle" logic that was previously scattered (and fragile) between `Camera` and the main loop.
  - `is_cursor_captured()` lets `Camera` (or other consumers) gate mouse-look application: rotation only happens while captured. Translation (WASD) and roll (Q/E) can remain active regardless of capture state (common dev expectation).
  - `reset_mouse_state()` is the public hook for the reset behavior (also called internally by `set_cursor_captured`).

- **Position query**: `get_mouse_position()` is available for any future needs (e.g. UI) but is not required for the core 6DOF camera controls.

## Data Flow (Text Diagram)

```
GLFW window events
       │
       ▼
cursor_position_callback / key_callback / mouse_button_callback
  (static, via glfwSet*Callback + user pointer)
       │
       ▼
InputManager (raw state)
  - keys_[], mouse_buttons_[]
  - mouse_position_, last_mouse_position_
  - raw_mouse_delta_ += (new - last)   // accumulation, sub-frame safe
       │
       │  (once per frame, after glfwPollEvents)
       ▼
InputManager::update(float delta_time)
  - acceleration = 1.0 + (mag(raw) * scale)
  - accelerated = raw * acceleration
  - smoothed = alpha * accelerated + (1-alpha) * previous_smoothed
  - raw = {0,0}   // ready for next frame's callbacks
       │
       │  (queries from consumers)
       ▼
Camera::update(delta_time, InputManager& input)  [or via singleton]
  if (input.is_cursor_captured())
      delta = input.get_mouse_delta()
      apply yaw (around current Up) and pitch (around current Right, with invert_pitch)
  apply WASD/Space/Shift using input.is_key_down(...)  (always, or gated as desired)
  apply Q/E roll using input.is_key_down(...)
       │
       ▼
Camera orientation + position updated (quaternion 6DOF)
       │
       ▼
get_view_matrix() / get_projection_matrix()  (used by renderer)
```

## Configuration & Ownership Split

- **Low-level processing tunables** (in `InputManager`):
  - `smoothing_alpha`
  - `acceleration_scale`
  - Reasonable defaults chosen for comfortable desktop feel (documented in the implementation + code comments). Setters provided for experimentation.

- **View / control tunables** (remain in `scene::Camera`):
  - `mouse_sensitivity` (applied to the already-smoothed delta)
  - `invert_pitch`
  - `movement_speed`
  - `fov_degrees`, near/far, etc.
  - These are "feel" parameters for the camera behavior, not raw input processing.

- Rationale: Keeps `InputManager` focused on "what the hardware/OS delivered, processed for quality" and `Camera` focused on "how that input moves the 6DOF view." Avoids duplicating sensitivity/invert logic in two places.

## Capture Toggle & Reset Strategy (Key Quality Detail)

When the user presses Escape (or any future mechanism):
1. Main loop (or a higher input consumer) detects the edge.
2. Calls `input.set_cursor_captured(!current)`.
3. Inside `set_cursor_captured`:
   - `glfwSetInputMode(...)` (shows/hides OS cursor, enables/disables raw delta mode).
   - Snapshot the *current* reported cursor position as `last_mouse_position_`.
   - Zero both `raw_mouse_delta_` and `smoothed_mouse_delta_`.
   - Set the one-shot `suppress_next_mouse_delta_` flag (consumed by first post-reset captured cb or by the next `update`).
4. The large-delta guard in the cursor cb (`mouse_delta_threshold_`) discards any *large* single-event jump (warps on toggle etc.). Small deltas are accumulated even for the first cb after reset (i.e. the user's first movement after pressing Escape contributes immediately; only large first deltas are dropped).
5. `update` forces a clean zero `smoothed_mouse_delta_` while the suppress flag is still set (covers the window before the first cb arrives). The flag is expired either by the cb or by `update` itself.
6. `Camera` (gated on `is_cursor_captured()`) sees either a clean zero or real post-toggle movement with no "lost first move" for modest user input.

This is more robust than the pre-refactor `first_mouse` flag that lived inside `Camera` and had to be manually poked from the main loop. (See implementation in `core/InputManager` for the exact suppress/expiry + size-guard interaction.)

## Relationship to Existing Code (Pre-Refactor Baseline)

Before this architecture:
- All input lived inside `scene::Camera` (ctor took `GLFWwindow*`, `update_desktop` called `glfwGetKey` / `glfwGetCursorPos` directly, maintained `last_mouse_x/y` + `first_mouse`).
- `AeroBoar.cpp` (the thin app shell) did the window creation, poll, and a few direct key checks for Escape/R + manual `glfwSetInputMode`.
- Mouse look was inside an `if (GLFW_CURSOR_DISABLED)` guard.
- The "enhanced mouse" work (commit caaacf7 and follow-ups) improved the quaternion math, made movement fully camera-relative, added R-frame and better roll, but still used the polling model.

This architecture document + the implementation plan describe the extraction of the input concerns into `core::` while preserving (and improving) the exact public control feel documented in `Camera.h`.

## Future Evolution Notes

- When OpenXR arrives, `CameraMode::VR` will bypass the desktop `InputManager` path entirely (head tracking + controller input come from the runtime).
- If we add a debug UI (ImGui), mouse button state + position will be consumed by the UI layer when the cursor is not captured; the manager already tracks them.
- The explicit source list in `CMakeLists.txt` means new `.cpp` files in `core/` must be added manually (documented as a minor hygiene item).

## References

- Implementation plan: `docs/architecture/desktop-input-implementation.md` (phases, exact file edits, verification steps).
- Current `scene::Camera` public contract & control description: `src/scene/Camera.h` (class doc comment).
- Tech context for input: `docs/agents/tech_context.md` ("Input & Camera" section).
- Project rules: `AGENTS.md` (namespaces, `#pragma once`, data-oriented style, canonical doc updates).

---

**Last updated**: During Phase 0 of the desktop input recovery implementation (see session plan for exact date and context). Update this file when the implementation or design evolves.