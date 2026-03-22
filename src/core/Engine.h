#pragma once

namespace Core {
struct Renderer;
class Engine {
  public:
    static void initialize(Renderer &renderer);
    static void render(Renderer &renderer);
    static void destroy(Renderer &renderer);

  private:
    static void init_vulkan(Renderer &renderer);
    static void init_vma(Renderer &renderer);
    static void destroy_vulkan(Renderer &renderer);
    static void destroy_vma(Renderer &renderer);
};
}; // namespace Core