#pragma once

#include "core/InputFrame.h"

namespace core {
class InputManager;
}

namespace scene {
class Camera;
class SceneManager;
}

namespace physics {
class PhysicsWorld;
}

namespace ecs {

// App / editor tooling hotkeys — not attached to Player.
// Debug + Release for now (plan decision #3 / #4).
struct EditorHotkeyContext {
    core::InputManager* input = nullptr;
    scene::Camera* camera = nullptr;
    scene::SceneManager* scene = nullptr;
    physics::PhysicsWorld* physics = nullptr;
    bool* stats_hud = nullptr; // Engine::frame_stats.hud_enabled (F4)
};

void editor_hotkey_system_update(const core::InputFrame& frame,
                                 EditorHotkeyContext& ctx);

} // namespace ecs
