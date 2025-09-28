#include "platforms/windows/window.hpp"
#include "core/renderer.hpp"
#include <iostream>
#include <stdexcept>

namespace aero_boar {

int WindowsMain() {
    try {
        // Create window
        Window window(800, 600, "Aero Boar Engine - Phase 1");
        
        // Initialize renderer
        Renderer renderer;
        if (!renderer.Initialize(window.GetGLFWWindow())) {
            std::cerr << "Failed to initialize renderer" << std::endl;
            return -1;
        }

        // Set renderer reference in window for resize callbacks
        window.SetRenderer(&renderer);

        std::cout << "Starting main loop..." << std::endl;

        // Main loop
        while (!window.ShouldClose()) {
            window.PollEvents();
            
            renderer.BeginFrame();
            renderer.Render();
            renderer.EndFrame();
        }

        std::cout << "Shutting down..." << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
}

} // namespace aero_boar

// Windows entry point
int main() {
    return aero_boar::WindowsMain();
}
