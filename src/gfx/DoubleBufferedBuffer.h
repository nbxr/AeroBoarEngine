#pragma once

#include "gfx/AllocatedBuffer.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace gfx {

// Shared growth math for double-buffered element arrays.
[[nodiscard]] inline uint32_t next_grown_capacity(uint32_t current_max,
                                                  uint32_t needed,
                                                  uint32_t growth_step) {
    if (needed <= current_max)
        return current_max;
    uint32_t step = growth_step > 0 ? growth_step : 50u;
    const uint32_t overflow = needed - current_max;
    const uint32_t chunks = 1u + (overflow / step);
    return current_max + chunks * step;
}

// Upload/render pair of VMA buffers (indices: upload starts at 1, render at 0).
// Matches the historical MaterialManager / SceneManager / MeshManager pattern:
// resize only grows the *upload* side; toggle swaps roles after a successful upload.
class DoubleBufferedBuffer {
  public:
    bool initialize(VkDevice device, VmaAllocator allocator, VkDeviceSize size_bytes,
                    VkBufferUsageFlags extra_usage = 0);

    void destroy(VkDevice device, VmaAllocator allocator);

    void toggle() {
        render_ ^= 1u;
        upload_ = render_ ^ 1u;
    }

    [[nodiscard]] AllocatedBuffer& upload() { return buffers_[upload_]; }
    [[nodiscard]] AllocatedBuffer& render() { return buffers_[render_]; }
    [[nodiscard]] const AllocatedBuffer& upload() const { return buffers_[upload_]; }
    [[nodiscard]] const AllocatedBuffer& render() const { return buffers_[render_]; }

    [[nodiscard]] uint32_t upload_index() const { return upload_; }
    [[nodiscard]] uint32_t render_index() const { return render_; }

    // Element capacity tracked per side (updated only when that side is resized).
    [[nodiscard]] uint32_t max_elements(uint32_t side) const {
        return max_elements_[side];
    }
    [[nodiscard]] uint32_t max_elements_upload() const {
        return max_elements_[upload_];
    }
    void set_max_elements(uint32_t side, uint32_t n) { max_elements_[side] = n; }
    void set_max_elements_both(uint32_t n) { max_elements_ = {n, n}; }

    // Grow upload side if needed for `needed` elements of `element_size`.
    // Copies `copy_bytes` from `copy_src` into the new mapping (may be null/0).
    bool ensure_element_capacity(VkDevice device, VmaAllocator allocator,
                                 uint32_t needed, uint32_t element_size,
                                 uint32_t growth_step, const void* copy_src,
                                 size_t copy_bytes,
                                 VkBufferUsageFlags extra_usage = 0);

    // Grow upload side to at least `needed_bytes` (for non-element streams like verts).
    bool ensure_byte_capacity(VkDevice device, VmaAllocator allocator,
                              VkDeviceSize needed_bytes, VkDeviceSize growth_pad,
                              const void* copy_src, size_t copy_bytes,
                              VkBufferUsageFlags extra_usage = 0);

    // memcpy CPU array into upload mapping (no resize).
    void upload_memcpy(const void* src, size_t bytes);

    bool bind_render_descriptor(VkDevice device, VkDescriptorSet set,
                                uint32_t binding, VkDeviceSize used_bytes) const;

  private:
    std::array<AllocatedBuffer, 2> buffers_{};
    std::array<uint32_t, 2> max_elements_{0, 0};
    uint32_t upload_ = 1;
    uint32_t render_ = 0;
    VkBufferUsageFlags extra_usage_ = 0;
};

} // namespace gfx
