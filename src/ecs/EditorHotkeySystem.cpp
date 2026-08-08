#include "ecs/EditorHotkeySystem.h"
#include "core/InputManager.h"
#include "core/Log.h"
#include "physics/PhysicsWorld.h"
#include "scene/Camera.h"
#include "scene/SceneManager.h"
#include <glm/glm.hpp>

namespace ecs {

void editor_hotkey_system_update(const core::InputFrame& frame,
                                 EditorHotkeyContext& ctx) {
    if (!ctx.input || !ctx.camera)
        return;

    // Escape: toggle mouse capture (desktop only).
    if (frame.escape_pressed) {
        if (ctx.camera->get_mode() == scene::CameraMode::Desktop) {
            const bool currently = ctx.input->is_cursor_captured();
            ctx.input->set_cursor_captured(!currently);
        }
    }

    // R: restore load pose; Shift+R: AABB frame scene.
    if (frame.r_pressed) {
        if (frame.shift_held() && ctx.scene) {
            auto [center, radius] = ctx.scene->get_scene_framing_sphere();
            ctx.camera->frame(center, radius);
        } else {
            ctx.camera->restore_initial_pose();
        }
    }

    // P: log camera pose.
    if (frame.p_pressed) {
        const glm::vec3 pos = ctx.camera->get_position();
        const glm::vec3 fwd = ctx.camera->get_forward();
        LOG_INFO("[Camera] pos=(" << pos.x << ", " << pos.y << ", " << pos.z
                 << ") forward=(" << fwd.x << ", " << fwd.y << ", " << fwd.z
                 << ")");
    }

    // N: cycle exclusive glTF animation clip.
    if (frame.n_pressed && ctx.scene) {
        auto& anims = ctx.scene->animations();
        if (anims.clip_count() > 0) {
            const uint32_t idx = anims.cycle_next_clip(true);
            if (idx != ~0u) {
                LOG_INFO("[Anim] Active clip [" << idx << "] '"
                         << anims.clip(idx).name << "'");
            }
        }
    }

    // F3: toggle physics collider wireframes.
    if (frame.f3_pressed && ctx.physics) {
        const bool next = !ctx.physics->is_debug_draw_enabled();
        ctx.physics->set_debug_draw_enabled(next);
        LOG_INFO("[Physics] debug draw " << (next ? "ON" : "OFF")
                 << " (bodies=" << ctx.physics->body_count() << ")");
    }
}

} // namespace ecs
