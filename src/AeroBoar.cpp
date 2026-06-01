#include "AeroBoar.h"
#include <iostream>
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

        engine.render();
    }

    // cleanup and terminate
    engine.cleanup_scene();
    engine.destroy();
    glfwTerminate();
    return 0;
}

