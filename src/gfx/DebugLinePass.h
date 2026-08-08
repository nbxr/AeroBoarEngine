#pragma once

#include "gfx/AllocatedBuffer.h"
#include "physics/PhysicsWorld.h"
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace gfx {

// Minimal LINE_LIST overlay for physics (and future) debug geometry.
// Matches main-pass MSAA + depth; depth write off so meshes stay visible under wires.
class DebugLinePass {
  public:
    bool create(VkDevice device, VmaAllocator allocator, VkRenderPass render_pass,
                VkSampleCountFlagBits samples);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Upload + draw. No-op if empty or not created.
    void draw(VkCommandBuffer cmd, VkExtent2D extent, const glm::mat4& view_proj,
              const std::vector<physics::DebugVertex>& vertices);

    [[nodiscard]] bool is_ready() const { return pipeline_ != VK_NULL_HANDLE; }

  private:
    bool ensure_vertex_capacity(VkDevice device, VmaAllocator allocator,
                                size_t vertex_count);

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    AllocatedBuffer vertex_buffer_{};
    size_t vertex_capacity_ = 0;
};

} // namespace gfx
