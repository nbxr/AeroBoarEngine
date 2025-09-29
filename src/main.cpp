#include "core/window_interface.hpp"
#include "core/renderer.hpp"
#include "input/input_manager.hpp"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <chrono>

namespace aero_boar {

int WindowsMain() {
    try {
        // Create window using the abstract window interface
        auto window = WindowFactory::CreateWindow(WindowFactory::Type::Desktop);
        if (!window->Initialize(800, 600, "Aero Boar Engine - Phase 2.5")) {
            std::cerr << "Failed to initialize window" << std::endl;
            return -1;
        }
        
        // Initialize renderer
        Renderer renderer;
        if (!renderer.Initialize(window.get())) {
            std::cerr << "Failed to initialize renderer" << std::endl;
            return -1;
        }

        // Set up resize callback for renderer
        window->SetResizeCallback([&renderer](int width, int height) {
            renderer.OnWindowResize();
        });

        // Load cube model from glTF file (Phase 2)
        std::cout << "Loading cube model from glTF file..." << std::endl;
        
        // Try to find the assets directory by looking for it in common locations
        std::string assetPath = "assets/models/cube.glb";
        std::filesystem::path currentPath = std::filesystem::current_path();
        
        // Check if assets directory exists in current path
        if (!std::filesystem::exists(assetPath)) {
            // Try build/AeroBoarEngine/Debug/assets/models/cube.glb
            std::string buildPath = "build/AeroBoarEngine/Debug/assets/models/cube.glb";
            if (std::filesystem::exists(buildPath)) {
                assetPath = buildPath;
                std::cout << "Found assets in build directory" << std::endl;
            }
            // Try AeroBoarEngine/Debug/assets/models/cube.glb
            else {
                std::string exePath = "AeroBoarEngine/Debug/assets/models/cube.glb";
                if (std::filesystem::exists(exePath)) {
                    assetPath = exePath;
                    std::cout << "Found assets in executable directory" << std::endl;
                }
            }
        }
        
        std::cout << "Loading model from: " << assetPath << std::endl;
        if (!renderer.LoadModel(assetPath)) {
            std::cerr << "Failed to load cube model, continuing with triangle only" << std::endl;
        }

        std::cout << "Starting main loop..." << std::endl;

        // Set up input callbacks for input manager
        InputManager* inputManager = renderer.GetInputManager();
        
        if (inputManager) {
            // Set up callbacks using the abstract window interface
            window->SetMouseMoveCallback([inputManager](double xpos, double ypos) {
                inputManager->OnMouseMove(xpos, ypos);
            });
            
            window->SetKeyCallback([inputManager](int key, int scancode, int action, int mods) {
                inputManager->OnKeyPress(key, scancode, action, mods);
            });
            
            window->SetMouseButtonCallback([inputManager](int button, int action, int mods) {
                inputManager->OnMouseButton(button, action, mods);
            });
            
            window->SetScrollCallback([inputManager](double xoffset, double yoffset) {
                inputManager->OnScroll(xoffset, yoffset);
            });
        }
        
        // Hide cursor and capture it for first-person controls
        window->SetCursorMode(2); // GLFW_CURSOR_DISABLED

        // Timing variables
        auto lastTime = std::chrono::high_resolution_clock::now();

        // Main loop
        while (!window->ShouldClose()) {
            // Calculate delta time
            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
            lastTime = currentTime;
            
            window->PollEvents();
            
            // Update camera
            renderer.UpdateCamera(deltaTime);
            
            renderer.BeginFrame();
            renderer.Render();
            renderer.EndFrame();
        }

        std::cout << "Shutting down..." << std::endl;
        std::cout << "Main function cleanup starting..." << std::endl;
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
