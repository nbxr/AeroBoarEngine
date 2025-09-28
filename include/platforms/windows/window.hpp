#pragma once

#include <GLFW/glfw3.h>

namespace aero_boar {
class Renderer;

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

    void SetRenderer(Renderer* renderer);

private:
    GLFWwindow* m_window;
    int m_width;
    int m_height;
    Renderer* m_renderer = nullptr;

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
};

} // namespace aero_boar





