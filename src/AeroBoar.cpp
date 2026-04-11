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
    core::Renderer renderer{};

    // Create a windowed mode window and its vulkan context
    // Set GLFW window hints
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // Create the window
    renderer.window.glfw_handle = glfwCreateWindow(640, 480, "Aero Boar", NULL, NULL);
    if (!renderer.window.glfw_handle) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    // Make the window's context current
    glfwMakeContextCurrent(renderer.window.glfw_handle);
    glfwSwapInterval(1);

    // initialize
    if (!core::Engine::initialize(renderer)) {
        core::Engine::destroy(renderer);
        return -1; // Return an error code if initialization fails
    }

    // Main render loop
    while (!glfwWindowShouldClose(renderer.window.glfw_handle)) {
        // poll for window events
        glfwPollEvents();

        // handle resizing
        int width, height;
        glfwGetFramebufferSize(renderer.window.glfw_handle, &width, &height);
        if (width > 0 && height > 0) {
        
            // Only handle resizing if the new dimensions are valid
        if (width != renderer.window.width || height != renderer.window.height) {
            renderer.window.width = width;
            renderer.window.height = height;
            // Recreate swapchain and related resources here
            
        }

        } else {
            // window is minimized: pause rendering
        }
         
        core::Engine::render(renderer);
    }

    // cleanup and terminate
    core::Engine::destroy(renderer);
    glfwTerminate();
    return 0;
}

