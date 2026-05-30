#include "AeroBoar.h"
#include <iostream>
#define GLFW_INCLUDE_VULKAN
#include "gfx/Engine.h"
#include "gfx/Renderer.h"
#include "scene/Camera.h"
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
        engine.camera.update(delta_time);

        // Simple Escape handling to toggle cursor capture (desktop only)
        static bool escape_was_pressed = false;
        bool escape_pressed = glfwGetKey(engine.renderer.window.glfw_handle, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (escape_pressed && !escape_was_pressed) {
            if (engine.camera.get_mode() == scene::CameraMode::Desktop) {
                static bool cursor_captured = true;
                cursor_captured = !cursor_captured;
                glfwSetInputMode(engine.renderer.window.glfw_handle, GLFW_CURSOR,
                                 cursor_captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            }
        }
        escape_was_pressed = escape_pressed;

        engine.render();
    }

    // cleanup and terminate
    engine.cleanup_scene();
    engine.destroy();
    glfwTerminate();
    return 0;
}

