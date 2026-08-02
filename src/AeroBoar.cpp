#include "AeroBoar.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>
#define GLFW_INCLUDE_VULKAN
#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include "scene/Camera.h"
#include "core/InputManager.h"
#include "core/Log.h"
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
    engine.camera.movement_speed = 3.0f;
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

    // Save the exact camera pose that resulted from loading (glTF camera node
    // or the initial AABB frame). The 'R' key (and Shift+R for generic framing)
    // can restore it.
    engine.camera.save_initial_pose();

    // Start with cursor visible (not captured). The user can move the mouse over the
    // window with no effect on the camera. Once the mouse is positioned over the view,
    // press Escape to toggle capture on and begin mouse look. This avoids any jump
    // when the mouse first enters the window area. Escape always toggles capture on/off.
    core::InputManager::get_instance().set_cursor_captured(false);
    engine.camera.reset_mouse_state();

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
            LOG_INFO("[Main] Window size changed: " << engine.renderer.window.width << "x" << engine.renderer.window.height
                     << " -> " << width << "x" << height << " (triggering swapchain recreate)");
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

        // On the very first frame after load, force a mouse state reset. This clears
        // any deltas that may have accumulated during window creation, init, or load
        // (before the user has had a chance to position the mouse over the window and
        // explicitly toggle capture with Escape). The large-delta guards in
        // InputManager provide additional protection on capture changes.
        static bool first_frame_after_load = true;
        if (first_frame_after_load) {
            input.reset_mouse_state();
            engine.camera.reset_mouse_state();
            first_frame_after_load = false;
        }

        engine.camera.update(delta_time, input);

        // If the device was lost (DEVICE_LOST from acquire/submit/present/wait),
        // stop the render loop instead of spinning at full speed and flooding
        // the validation log with millions of repeated errors.
        if (engine.renderer.vk.device_lost) {
            LOG_ERROR("Device lost - exiting main loop to avoid log spam and further invalid calls.");
            glfwSetWindowShouldClose(engine.renderer.window.glfw_handle, GLFW_TRUE);
            break;
        }

        // Escape toggles mouse capture (cursor visible vs. look control).
        // At startup we begin uncaptured so the user can position the mouse over
        // the window without causing any camera movement or jumps. Once ready,
        // press Escape to capture and use the mouse to look around.
        static bool escape_was_pressed = false;
        bool escape_pressed = input.is_key_down(GLFW_KEY_ESCAPE);
        if (escape_pressed && !escape_was_pressed) {
            if (engine.camera.get_mode() == scene::CameraMode::Desktop) {
                bool currently = input.is_cursor_captured();
                input.set_cursor_captured(!currently);
            }
        }
        escape_was_pressed = escape_pressed;

        // R key handling (Escape is the toggle for mouse capture):
        //   R      -> restore the exact pose from when the scene finished loading
        //             (the glTF camera node if one was present, otherwise the
        //             initial framing used at load time).
        //   Shift+R -> perform a generic AABB-based framing of the model.
        static bool r_was_pressed = false;
        bool r_pressed = input.is_key_down(GLFW_KEY_R);
        if (r_pressed && !r_was_pressed) {
            bool shift = input.is_key_down(GLFW_KEY_LEFT_SHIFT) ||
                         input.is_key_down(GLFW_KEY_RIGHT_SHIFT);
            if (shift) {
                auto [center, radius] =
                    engine.renderer.scene_manager.get_scene_framing_sphere();
                engine.camera.frame(center, radius);
            } else {
                engine.camera.restore_initial_pose();
            }
        }
        r_was_pressed = r_pressed;

        engine.render();
    }

    // cleanup and terminate
    engine.cleanup_scene();
    engine.destroy();
    glfwTerminate();
    return 0;
}

