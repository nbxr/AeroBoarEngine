#pragma once

#include "core/window_interface.hpp"
#include <GLFW/glfw3.h>
#include <memory>

namespace aero_boar {

class DesktopWindow : public IWindow {
public:
    DesktopWindow();
    virtual ~DesktopWindow();

    // IWindow interface
    bool Initialize(int width, int height, const std::string& title) override;
    void Shutdown() override;
    bool ShouldClose() const override;
    void PollEvents() override;

    int GetWidth() const override;
    int GetHeight() const override;
    void SetTitle(const std::string& title) override;
    bool IsResized() const override;
    void ClearResizedFlag() override;

    void* GetNativeWindowHandle() const override;
    void* GetNativeDisplayHandle() const override;

    void SetMouseMoveCallback(MouseMoveCallback callback) override;
    void SetKeyCallback(KeyCallback callback) override;
    void SetMouseButtonCallback(MouseButtonCallback callback) override;
    void SetScrollCallback(ScrollCallback callback) override;
    void SetResizeCallback(ResizeCallback callback) override;

    void SetCursorMode(int mode) override;
    void SetUserPointer(void* pointer) override;
    void* GetUserPointer() const override;

#ifdef _WIN32
    GLFWwindow* GetGLFWWindow() const override;
#endif

private:
    GLFWwindow* m_window = nullptr;
    bool m_initialized = false;
    bool m_resized = false;
    int m_width = 0;
    int m_height = 0;

    // Callbacks
    MouseMoveCallback m_mouseMoveCallback;
    KeyCallback m_keyCallback;
    MouseButtonCallback m_mouseButtonCallback;
    ScrollCallback m_scrollCallback;
    ResizeCallback m_resizeCallback;

    // Static callback wrappers
    static void StaticMouseMoveCallback(GLFWwindow* window, double xpos, double ypos);
    static void StaticKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void StaticMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void StaticScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void StaticResizeCallback(GLFWwindow* window, int width, int height);
};

} // namespace aero_boar
