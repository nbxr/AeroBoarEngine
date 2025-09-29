#include "platforms/desktop/desktop_window.hpp"
#include <iostream>
#include <stdexcept>

namespace aero_boar {

DesktopWindow::DesktopWindow() {
}

DesktopWindow::~DesktopWindow() {
    Shutdown();
}

bool DesktopWindow::Initialize(int width, int height, const std::string& title) {
    if (m_initialized) {
        return true;
    }

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // Configure GLFW for Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // Create window
    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    m_width = width;
    m_height = height;

    // Set up callbacks
    glfwSetWindowUserPointer(m_window, this);
    glfwSetCursorPosCallback(m_window, StaticMouseMoveCallback);
    glfwSetKeyCallback(m_window, StaticKeyCallback);
    glfwSetMouseButtonCallback(m_window, StaticMouseButtonCallback);
    glfwSetScrollCallback(m_window, StaticScrollCallback);
    glfwSetFramebufferSizeCallback(m_window, StaticResizeCallback);

    m_initialized = true;
    std::cout << "Window created: " << width << "x" << height << std::endl;
    return true;
}

void DesktopWindow::Shutdown() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    
    if (m_initialized) {
        glfwTerminate();
        m_initialized = false;
    }
}

bool DesktopWindow::ShouldClose() const {
    return m_window ? glfwWindowShouldClose(m_window) : true;
}

void DesktopWindow::PollEvents() {
    if (m_window) {
        glfwPollEvents();
    }
}

int DesktopWindow::GetWidth() const {
    return m_width;
}

int DesktopWindow::GetHeight() const {
    return m_height;
}

void DesktopWindow::SetTitle(const std::string& title) {
    if (m_window) {
        glfwSetWindowTitle(m_window, title.c_str());
    }
}

bool DesktopWindow::IsResized() const {
    return m_resized;
}

void DesktopWindow::ClearResizedFlag() {
    m_resized = false;
}

void* DesktopWindow::GetNativeWindowHandle() const {
    return m_window;
}

void* DesktopWindow::GetNativeDisplayHandle() const {
    // GLFW doesn't expose display handle on Windows
    return nullptr;
}

void DesktopWindow::SetMouseMoveCallback(MouseMoveCallback callback) {
    m_mouseMoveCallback = callback;
}

void DesktopWindow::SetKeyCallback(KeyCallback callback) {
    m_keyCallback = callback;
}

void DesktopWindow::SetMouseButtonCallback(MouseButtonCallback callback) {
    m_mouseButtonCallback = callback;
}

void DesktopWindow::SetScrollCallback(ScrollCallback callback) {
    m_scrollCallback = callback;
}

void DesktopWindow::SetResizeCallback(ResizeCallback callback) {
    m_resizeCallback = callback;
}

void DesktopWindow::SetCursorMode(int mode) {
    if (m_window) {
        glfwSetInputMode(m_window, GLFW_CURSOR, mode);
    }
}

void DesktopWindow::SetUserPointer(void* pointer) {
    if (m_window) {
        glfwSetWindowUserPointer(m_window, pointer);
    }
}

void* DesktopWindow::GetUserPointer() const {
    return m_window ? glfwGetWindowUserPointer(m_window) : nullptr;
}

#ifdef _WIN32
GLFWwindow* DesktopWindow::GetGLFWWindow() const {
    return m_window;
}
#endif

// Static callback implementations
void DesktopWindow::StaticMouseMoveCallback(GLFWwindow* window, double xpos, double ypos) {
    DesktopWindow* desktopWindow = static_cast<DesktopWindow*>(glfwGetWindowUserPointer(window));
    if (desktopWindow && desktopWindow->m_mouseMoveCallback) {
        desktopWindow->m_mouseMoveCallback(xpos, ypos);
    }
}

void DesktopWindow::StaticKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    // Handle escape key to close window
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    
    DesktopWindow* desktopWindow = static_cast<DesktopWindow*>(glfwGetWindowUserPointer(window));
    if (desktopWindow && desktopWindow->m_keyCallback) {
        desktopWindow->m_keyCallback(key, scancode, action, mods);
    }
}

void DesktopWindow::StaticMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    DesktopWindow* desktopWindow = static_cast<DesktopWindow*>(glfwGetWindowUserPointer(window));
    if (desktopWindow && desktopWindow->m_mouseButtonCallback) {
        desktopWindow->m_mouseButtonCallback(button, action, mods);
    }
}

void DesktopWindow::StaticScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    DesktopWindow* desktopWindow = static_cast<DesktopWindow*>(glfwGetWindowUserPointer(window));
    if (desktopWindow && desktopWindow->m_scrollCallback) {
        desktopWindow->m_scrollCallback(xoffset, yoffset);
    }
}

void DesktopWindow::StaticResizeCallback(GLFWwindow* window, int width, int height) {
    DesktopWindow* desktopWindow = static_cast<DesktopWindow*>(glfwGetWindowUserPointer(window));
    if (desktopWindow) {
        desktopWindow->m_width = width;
        desktopWindow->m_height = height;
        desktopWindow->m_resized = true;
        
        if (desktopWindow->m_resizeCallback) {
            desktopWindow->m_resizeCallback(width, height);
        }
    }
}

} // namespace aero_boar
