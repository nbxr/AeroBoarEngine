#pragma once

namespace core {
struct Renderer;
class Engine {
  public:
    static void initialize(core::Renderer &renderer);
    static void render(core::Renderer &renderer);
    static void destroy(core::Renderer &renderer);

  private:
    static void init_vulkan(core::Renderer &renderer);
    static void init_vma(core::Renderer &renderer);
    static void init_renderer(core::Renderer &renderer);
    static void destroy_render_pass(core::Renderer &renderer);
    static void destroy_vulkan(core::Renderer &renderer);
    static void destroy_vma(core::Renderer &renderer);
};
}; // namespace Core