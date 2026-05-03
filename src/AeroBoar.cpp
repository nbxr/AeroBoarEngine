#include "AeroBoar.h"
#include <iostream>
#define GLFW_INCLUDE_VULKAN
#include "core/Engine.h"
#include "core/Renderer.h"
#include <GLFW/glfw3.h>

int AeroBoar::fly() {

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // create renderer
    core::Engine engine{};

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
    while (!glfwWindowShouldClose(engine.renderer.window.glfw_handle)) {
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
         
        engine.render();
    }

    // cleanup and terminate
    engine.cleanup_scene();
    engine.destroy();
    glfwTerminate();
    return 0;
}

