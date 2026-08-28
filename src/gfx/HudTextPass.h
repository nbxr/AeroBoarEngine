#pragma once

#include "gfx/AllocatedBuffer.h"
#include "gfx/AllocatedImage.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace gfx {

// Where a text run lives. Same glyph layout; only the clip matrix changes.
// Screen: desktop overlay, pixels, top-left origin, +Y down.
// View: head-locked HUD plane in camera space (meters, +Y up, -Z forward).
//       VR stereo later: same verts, per-eye view-proj.
enum class HudSpace : uint8_t { Screen = 0, View = 1 };

// Bitmap-font overlay. Draws a 1x swapchain pass after scene/WBOIT so text
// sits on top. Not part of the PBR bindless set (own atlas + sampler).
class HudTextPass {
  public:
    bool create(VkDevice device, VmaAllocator allocator, VkQueue graphics_queue,
                VkCommandPool cmd_pool, VkFormat swap_format);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Rebuild overlay framebuffers after swapchain recreate.
    bool set_swapchain(VkDevice device, VkExtent2D extent,
                       const std::vector<VkImageView>& swap_views);

    void begin_frame();
    void add_text(const char* text, float x, float y, float pixel_or_meter_height,
                  const glm::vec4& color, HudSpace space = HudSpace::Screen);

    [[nodiscard]] bool has_text() const {
        return !screen_verts_.empty() || !view_verts_.empty();
    }
    [[nodiscard]] bool is_ready() const { return pipeline_ != VK_NULL_HANDLE; }

    // Overlay RP: LOAD swapchain color, end PRESENT. Call while the image is
    // COLOR_ATTACHMENT_OPTIMAL (barrier from PRESENT if WBOIT already flipped it).
    void draw(VkCommandBuffer cmd, uint32_t frame_index, uint32_t image_index,
              VkExtent2D extent, const glm::mat4& camera_proj,
              float view_plane_distance = 1.2f);

  private:
    struct Vertex {
        glm::vec2 pos;
        glm::vec2 uv;
        glm::vec4 color;
    };

    bool create_overlay_pass(VkDevice device, VkFormat format);
    bool create_pipeline(VkDevice device);
    bool create_atlas(VkDevice device, VmaAllocator allocator, VkQueue queue,
                      VkCommandPool pool);
    bool ensure_vertex_capacity(uint32_t frame_index, size_t vertex_count);
    void emit_string(std::vector<Vertex>& out, const char* text, float x, float y,
                     float height, const glm::vec4& color, bool y_down);

    static constexpr uint32_t kMaxFrames = 2;
    static constexpr int kGlyphPx = 8;
    static constexpr int kAtlasCols = 16;
    static constexpr int kAtlasRows = 6; // ASCII 32..127
    static constexpr int kAtlasW = kGlyphPx * kAtlasCols;
    static constexpr int kAtlasH = kGlyphPx * kAtlasRows;

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;
    VkFormat swap_format_ = VK_FORMAT_UNDEFINED;

    VkRenderPass overlay_rp_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    AllocatedImage atlas_{};
    std::vector<VkFramebuffer> framebuffers_{};

    std::array<AllocatedBuffer, kMaxFrames> vertex_buffers_{};
    std::array<size_t, kMaxFrames> vertex_capacity_{};

    std::vector<Vertex> screen_verts_{};
    std::vector<Vertex> view_verts_{};
};

} // namespace gfx
