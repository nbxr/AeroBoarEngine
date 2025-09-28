#pragma once

#include <GLFW/glfw3.h>

namespace aero_boar {

class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();

    bool ShouldClose() const;
    void PollEvents();
    void SwapBuffers();
    GLFWwindow* GetGLFWWindow() const;

    int GetWidth() const;
    int GetHeight() const;

private:
    GLFWwindow* m_window;
    int m_width;
    int m_height;

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
};

} // namespace aero_boar





