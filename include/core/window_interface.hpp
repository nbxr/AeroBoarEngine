#pragma once

#include <string>
#include <functional>
#include <memory>

#ifdef _WIN32
#include <GLFW/glfw3.h>
#endif

namespace aero_boar {

// Abstract window interface for cross-platform compatibility
class IWindow {
public:
    virtual ~IWindow() = default;

    // Window lifecycle
    virtual bool Initialize(int width, int height, const std::string& title) = 0;
    virtual void Shutdown() = 0;
    virtual bool ShouldClose() const = 0;
    virtual void PollEvents() = 0;

    // Window properties
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual void SetTitle(const std::string& title) = 0;
    virtual bool IsResized() const = 0;
    virtual void ClearResizedFlag() = 0;

    // Platform-specific access (for Vulkan surface creation)
    virtual void* GetNativeWindowHandle() const = 0;
    virtual void* GetNativeDisplayHandle() const = 0;

    // Input callbacks
    using MouseMoveCallback = std::function<void(double x, double y)>;
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    using ScrollCallback = std::function<void(double xoffset, double yoffset)>;
    using ResizeCallback = std::function<void(int width, int height)>;

    virtual void SetMouseMoveCallback(MouseMoveCallback callback) = 0;
    virtual void SetKeyCallback(KeyCallback callback) = 0;
    virtual void SetMouseButtonCallback(MouseButtonCallback callback) = 0;
    virtual void SetScrollCallback(ScrollCallback callback) = 0;
    virtual void SetResizeCallback(ResizeCallback callback) = 0;

    // Input state
    virtual void SetCursorMode(int mode) = 0; // 0 = normal, 1 = hidden, 2 = disabled
    virtual void SetUserPointer(void* pointer) = 0;
    virtual void* GetUserPointer() const = 0;

    // Platform-specific getters (for backward compatibility during transition)
#ifdef _WIN32
    virtual GLFWwindow* GetGLFWWindow() const = 0;
#endif
};

// Window creation factory
class WindowFactory {
public:
    enum class Type {
        Desktop,
        VR
    };

    static std::unique_ptr<IWindow> CreateWindow(Type type);
};

} // namespace aero_boar
