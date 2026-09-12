#include "AeroBoar.h"
#include <iostream>
#include <thread>
#include <chrono>
#define GLFW_INCLUDE_VULKAN
#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include "scene/Camera.h"
#include "core/InputManager.h"
#include "core/InputFrame.h"
#include "core/Configuration.h"
#include "core/Log.h"
#include "core/FrameStats.h"
#include "core/Profiler.h"
#include "ecs/DesktopMoveSystem.h"
#include "ecs/FpsMoveSystem.h"
#include "ecs/LocomotionAnimSystem.h"
#include "ecs/EditorHotkeySystem.h"
#include "ecs/ScriptSystem.h"
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <GLFW/glfw3.h>

int AeroBoar::fly() {

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // create renderer
    gfx::Engine engine{};

    // Create a windowed mode window and its vulkan context
    // Set GLFW window hints
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // Create the window
    engine.renderer.window.glfw_handle = glfwCreateWindow(640, 480, "Aero Boar", NULL, NULL);
    if (!engine.renderer.window.glfw_handle) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Initialize tracked window size from the actual framebuffer size right after
    // creation. This prevents the main loop's size check from unconditionally
    // triggering a recreate_swapchain() on the very first frame (because the
    // Renderer struct defaults width/height to 0). The early recreate was
    // correlating with the immediate DEVICE_LOST on the first present in the
    // latest run (even with 3 swapchain images).
    int initial_width, initial_height;
    glfwGetFramebufferSize(engine.renderer.window.glfw_handle, &initial_width, &initial_height);
    engine.renderer.window.width = initial_width;
    engine.renderer.window.height = initial_height;

    // Initialize camera (desktop mode by default)
    engine.camera = scene::Camera(engine.renderer.window.glfw_handle);
    engine.camera.set_mode(scene::CameraMode::Desktop);
    engine.camera.mouse_sensitivity = 0.05f;
    engine.camera.movement_speed = 0.30f;
    engine.camera.roll_speed = 15.0f;
    core::InputManager::get_instance().initialize(engine.renderer.window.glfw_handle);
    // NOTE: We start with capture off (see post-load code). The mouse can be
    // moved over the window with no camera effect. The user presses Escape to
    // toggle capture on once the pointer is positioned over the view. This
    // completely avoids the "jump when mouse first enters the window" problem.

    // Make the window's context current
    glfwMakeContextCurrent(engine.renderer.window.glfw_handle);
    glfwSwapInterval(1);

    // initialize
    if (!engine.initialize()) {
        LOG_ERROR("Engine initialize() failed");
        engine.destroy();
        glfwTerminate();
        return -1;
    }

    // load the default scene defined in configuration.yaml
    if (!engine.load_default_scene()) {
        std::cerr << "Failed to load default scene" << std::endl;
        engine.destroy();
        glfwTerminate();
        return -1;
    }

    // Physics collider wireframes (F3 toggles at runtime). Scene KHR bodies
    // are built during load_scene; kill floor is configured there too.
    {
        const nlohmann::json& root = core::Configuration::get_root();
        bool phys_debug = false;
        if (root.contains("physicsDebugDraw") && root["physicsDebugDraw"].is_boolean())
            phys_debug = root["physicsDebugDraw"].get<bool>();
        engine.physics.set_debug_draw_enabled(phys_debug);
        if (phys_debug)
            LOG_INFO("[Physics] debug draw ON (config physicsDebugDraw; F3 to toggle)");
    }

    // Optional config override for startup camera (Hi-Z / cull debug).
    // Schema:
    //   "cameraOverride": { "enabled": true, "position": [x,y,z], "forward": [x,y,z] }
    // or an array of one such object (same as validationFeatures style).
    {
        const nlohmann::json& root = core::Configuration::get_root();
        if (root.contains("cameraOverride") && !root["cameraOverride"].is_null()) {
            const nlohmann::json* node = &root["cameraOverride"];
            if (node->is_array() && !node->empty())
                node = &(*node)[0];
            if (node->is_object() && node->value("enabled", false) &&
                node->contains("position") && (*node)["position"].is_array() &&
                (*node)["position"].size() >= 3 &&
                node->contains("forward") && (*node)["forward"].is_array() &&
                (*node)["forward"].size() >= 3) {
                const auto& p = (*node)["position"];
                const auto& f = (*node)["forward"];
                const glm::vec3 pos(p[0].get<float>(), p[1].get<float>(),
                                    p[2].get<float>());
                const glm::vec3 fwd(f[0].get<float>(), f[1].get<float>(),
                                    f[2].get<float>());
                engine.camera.set_position_and_forward(pos, fwd);
                LOG_INFO("[Camera] Override from configuration.json: pos=("
                         << pos.x << ", " << pos.y << ", " << pos.z
                         << ") forward=(" << fwd.x << ", " << fwd.y << ", "
                         << fwd.z << ")");
            }
        }
    }

    // Save the exact camera pose that resulted from loading (glTF camera node
    // or the initial AABB frame), after any cameraOverride. The 'R' key
    // (and Shift+R for generic framing) can restore it.
    engine.camera.save_initial_pose();

    // ECS world populated during load_scene (entities + optional extras.ECS_Components_v1).
    // Start with cursor visible (not captured). Escape (EditorHotkeySystem) toggles.
    core::InputManager::get_instance().set_cursor_captured(false);
    engine.camera.reset_mouse_state();

    core::InputFrameBuilder input_frames;

    // Main render loop — order matches ecs-plan §9
    double last_frame_time = glfwGetTime();

    while (!glfwWindowShouldClose(engine.renderer.window.glfw_handle)) {
        double current_time = glfwGetTime();
        float delta_time = static_cast<float>(current_time - last_frame_time);
        last_frame_time = current_time;

        // render() ends the CPU frame; if we skip it (minimize / recover), the
        // destructor writes the snapshot so the HUD still has last-frame times.
        struct CpuFrameGuard {
            core::FrameStats& stats;
            bool armed = true;
            explicit CpuFrameGuard(core::FrameStats& s) : stats(s) {
                s.begin_cpu_frame();
            }
            void disarm() { armed = false; }
            ~CpuFrameGuard() {
                if (armed)
                    stats.end_cpu_frame();
            }
        } cpu_frame{engine.frame_stats};

        int width = 0;
        int height = 0;
        core::InputFrame frame{};
        {
            auto input_scope = engine.frame_stats.scope(core::CpuStage::Input);
            glfwPollEvents();
            auto& input = core::InputManager::get_instance();
            input.update(delta_time);

            glfwGetFramebufferSize(engine.renderer.window.glfw_handle, &width,
                                   &height);
            if (width > 0 && height > 0) {
                if (width != engine.renderer.window.width ||
                    height != engine.renderer.window.height) {
                    LOG_INFO("[Main] Window size changed: "
                             << engine.renderer.window.width << "x"
                             << engine.renderer.window.height << " -> " << width
                             << "x" << height << " (triggering swapchain recreate)");
                    engine.renderer.window.width = width;
                    engine.renderer.window.height = height;
                    engine.recreate_swapchain();
                }
            }

            static bool first_frame_after_load = true;
            if (first_frame_after_load) {
                input.reset_mouse_state();
                engine.camera.reset_mouse_state();
                input_frames.reset_edges();
                first_frame_after_load = false;
            }

            // 3) InputFrame  4) EditorHotkeys  5) DesktopMove (player)
            frame = input_frames.build(input, delta_time);

            ecs::EditorHotkeyContext editor_ctx{};
            editor_ctx.input = &input;
            editor_ctx.camera = &engine.camera;
            editor_ctx.scene = &engine.renderer.scene_manager;
            editor_ctx.physics = &engine.physics;
            editor_ctx.stats_hud = &engine.frame_stats.hud_enabled;
            ecs::editor_hotkey_system_update(frame, editor_ctx);
        }

        // Move first so LocomotionAnim sees this-frame speed; player-root
        // channels are masked so clips cannot overwrite FpsMove.
        {
            auto sim = engine.frame_stats.scope(core::CpuStage::Simulate);
            AERO_ZONE_NAMED("sim.logic");
            auto& tw = engine.renderer.scene_manager.transforms();
            const ecs::Entity ap = engine.ecs_world.active_player();
            // Any live player body is a character controller (WASD + 1st/3rd
            // cam). Free-fly is only for the inspector player (no TransformLink).
            bool used_fps = false;
            if (ap != ecs::kInvalidEntity &&
                engine.ecs_world.is_alive(ap) &&
                engine.ecs_world.player_tags.has(ap)) {
                const ecs::TransformLink* link =
                    engine.ecs_world.transform_links.try_get(ap);
                if (link && link->transform_index != ~0u &&
                    tw.is_alive(link->transform_index)) {
                    engine.ecs_world.fps_moves.get_or_emplace(ap);
                    ecs::fps_move_system_update(engine.ecs_world, frame,
                                                engine.camera, tw,
                                                &engine.physics);
                    used_fps = true;
                }
            }
            if (!used_fps) {
                ecs::desktop_move_system_update(engine.ecs_world, frame,
                                                engine.camera, &tw);
            }

            ecs::locomotion_anim_system_update(
                engine.ecs_world, engine.renderer.scene_manager.animations());
            engine.update_animations(delta_time);
            ecs::script_system_update(engine.ecs_world, delta_time);
        }

        {
            auto phys = engine.frame_stats.scope(core::CpuStage::Physics);
            engine.step_physics(delta_time);
        }

        if (engine.renderer.vk.device_lost) {
            if (!engine.try_recover_gpu()) {
                LOG_ERROR("GPU recovery failed — exiting");
                glfwSetWindowShouldClose(engine.renderer.window.glfw_handle,
                                         GLFW_TRUE);
                break;
            }
            continue;
        }

        // Overlay stats are filled inside Engine::render() after GPU collect.
        // Minimized / zero-extent: skip present (DWM/RDP often reports 0x0).
        if (width > 0 && height > 0) {
            cpu_frame.disarm();
            engine.render();
        }
        FrameMark;

        if (engine.renderer.vk.device_lost) {
            if (!engine.try_recover_gpu()) {
                LOG_ERROR("GPU recovery failed — exiting");
                glfwSetWindowShouldClose(engine.renderer.window.glfw_handle,
                                         GLFW_TRUE);
                break;
            }
        }
    }

    // cleanup and terminate
    engine.cleanup_scene();
    engine.destroy();
    glfwTerminate();
    return 0;
}

