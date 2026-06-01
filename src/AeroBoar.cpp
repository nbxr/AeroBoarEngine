#include "AeroBoar.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#define GLFW_INCLUDE_VULKAN
#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include "scene/Camera.h"
#include "core/InputManager.h"
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

    // Initialize camera (desktop mode by default)
    engine.camera = scene::Camera(engine.renderer.window.glfw_handle);
    engine.camera.set_mode(scene::CameraMode::Desktop);
    core::InputManager::get_instance().initialize(engine.renderer.window.glfw_handle);
    core::InputManager::get_instance().set_cursor_captured(true);

    // Make the window's context current
    glfwMakeContextCurrent(engine.renderer.window.glfw_handle);
    glfwSwapInterval(1);

    // initialize
    if (!engine.initialize()) {
        engine.destroy();
        return -1; // Return an error code if initialization fails
    }

    // load the default scene defined in configuration.yaml
    if (!engine.load_default_scene()) {
        std::cerr << "Failed to load default scene" << std::endl;
        engine.destroy();
        glfwTerminate();
        return -1;
    }

    // Reset mouse input tracking as late as possible — after the window is
    // created, after capture was enabled, after full Vulkan initialization,
    // and after we have placed the camera at the authored glTF pose.
    // This is the last moment before we enter the loop that calls camera.update()
    // on every frame.  Doing it only inside load_scene was too early; mouse
    // deltas from window creation + cursor capture can still accumulate
    // between load_scene returning and the first camera.update().
    engine.camera.reset_mouse_state();
    printf("[INPUT] Mouse state reset immediately before main loop (after authored camera pose).\n");

    // Main render loop
    double last_frame_time = glfwGetTime();

    while (!glfwWindowShouldClose(engine.renderer.window.glfw_handle)) {
        double current_time = glfwGetTime();
        float delta_time = static_cast<float>(current_time - last_frame_time);
        last_frame_time = current_time;

        // poll for window events
        glfwPollEvents();
        core::InputManager::get_instance().update(delta_time);

        // handle resizing
        int width, height;
        glfwGetFramebufferSize(engine.renderer.window.glfw_handle, &width, &height);
        if (width > 0 && height > 0) {
        
            // Only handle resizing if the new dimensions are valid
        if (width != engine.renderer.window.width || 
            height != engine.renderer.window.height) {
            engine.renderer.window.width = width;
            engine.renderer.window.height = height;
            // Recreate swapchain and related resources here
            engine.recreate_swapchain();
        }

        } else {
            // window is minimized: pause rendering
        }

        // Update camera (desktop WASD + mouse for now)
        auto& input = core::InputManager::get_instance();

        // Belt-and-suspenders: on the absolute first frame after load, ignore
        // any mouse delta that might have arrived between the late reset above
        // and this first camera.update().  This guarantees the authored glTF
        // camera pose survives until the user deliberately moves the mouse.
        static bool first_frame_after_load = true;
        if (first_frame_after_load) {
            input.reset_mouse_state();           // extra safety
            engine.camera.reset_mouse_state();
            first_frame_after_load = false;
            printf("[INPUT] First frame after load — forcing zero mouse delta for authored camera.\n");
        }

        engine.camera.update(delta_time, input);

        // Simple Escape handling to toggle cursor capture (desktop only)
        static bool escape_was_pressed = false;
        bool escape_pressed = input.is_key_down(GLFW_KEY_ESCAPE);
        if (escape_pressed && !escape_was_pressed) {
            if (engine.camera.get_mode() == scene::CameraMode::Desktop) {
                bool currently = input.is_cursor_captured();
                input.set_cursor_captured(!currently);
                // set_cursor_captured() internally resets mouse tracking to prevent jumps.
            }
        }
        escape_was_pressed = escape_pressed;

        // R key: re-frame camera on the first object in the scene (debug)
        static bool r_was_pressed = false;
        bool r_pressed = input.is_key_down(GLFW_KEY_R);
        if (r_pressed && !r_was_pressed) {
            auto [center, radius] = engine.renderer.scene_manager.get_first_instance_framing_sphere();
            engine.camera.frame(center, radius);
            printf("[Camera] Reframed using AABB: center=(%.2f, %.2f, %.2f) radius=%.2f\n",
                   center.x, center.y, center.z, radius);
        }
        r_was_pressed = r_pressed;

        // P key: Print current camera pose + delta from the pose it had right after scene load.
        // Extremely useful when you have to hunt for the model with the authored camera.
        static bool p_was_pressed = false;
        static bool initial_captured = false;
        static glm::vec3 initial_pos{0.0f};
        static glm::vec3 initial_look{0.0f};

        bool p_pressed = input.is_key_down(GLFW_KEY_P);
        if (p_pressed && !p_was_pressed) {
            glm::vec3 cur_pos = engine.camera.get_position();
            glm::vec3 cur_look = engine.camera.get_forward();

            if (!initial_captured) {
                initial_pos = cur_pos;
                initial_look = cur_look;
                initial_captured = true;
                printf("[CAMERA-POSE] === INITIAL (captured on first P or first frame) ===\n");
                printf("[CAMERA-POSE] pos=(%.6f, %.6f, %.6f) look=(%.6f, %.6f, %.6f)\n",
                       initial_pos.x, initial_pos.y, initial_pos.z,
                       initial_look.x, initial_look.y, initial_look.z);
            }

            glm::vec3 delta = cur_pos - initial_pos;
            float angle_diff = glm::degrees(std::acos(std::clamp(glm::dot(cur_look, initial_look), -1.0f, 1.0f)));

            printf("[CAMERA-POSE] CURRENT pos=(%.6f, %.6f, %.6f) look=(%.6f, %.6f, %.6f)\n",
                   cur_pos.x, cur_pos.y, cur_pos.z, cur_look.x, cur_look.y, cur_look.z);
            printf("[CAMERA-POSE] DELTA   pos_delta=(%.6f, %.6f, %.6f)  angle_from_initial=%.2f deg\n",
                   delta.x, delta.y, delta.z, angle_diff);
            printf("[CAMERA-POSE] (Use this + the full node hierarchy dump at load time to locate the helmet.)\n");
        }
        p_was_pressed = p_pressed;

        engine.render();
    }

    // cleanup and terminate
    engine.cleanup_scene();
    engine.destroy();
    glfwTerminate();
    return 0;
}

