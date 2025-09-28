#include "platforms/windows/window.hpp"
#include "core/renderer.hpp"
#include <iostream>
#include <stdexcept>

namespace aero_boar {

Window::Window(int width, int height, const char* title) 
    : m_width(width), m_height(height) {
    
    // Initialize GLFW
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // Configure GLFW for Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // Create window
    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    // Set callbacks
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, FramebufferSizeCallback);
    glfwSetKeyCallback(m_window, KeyCallback);

    std::cout << "Window created: " << width << "x" << height << std::endl;
}

Window::~Window() {
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
}

bool Window::ShouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void Window::PollEvents() {
    glfwPollEvents();
}

void Window::SwapBuffers() {
    // GLFW doesn't handle buffer swapping for Vulkan
    // This is handled by the Vulkan swapchain
}

GLFWwindow* Window::GetGLFWWindow() const {
    return m_window;
}

int Window::GetWidth() const {
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    return width;
}

int Window::GetHeight() const {
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    return height;
}

void Window::SetRenderer(Renderer* renderer) {
    m_renderer = renderer;
}

void Window::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    Window* win = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (win) {
        win->m_width = width;
        win->m_height = height;
        
        // Notify renderer of resize
        if (win->m_renderer) {
            win->m_renderer->OnWindowResize();
        }
    }
}

void Window::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

} // namespace aero_boar
