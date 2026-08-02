#include "gfx/DoubleBufferedBuffer.h"
#include "gfx/BufferUtils.h"

namespace gfx {

bool DoubleBufferedBuffer::initialize(VkDevice device, VmaAllocator allocator,
                                      VkDeviceSize size_bytes,
                                      VkBufferUsageFlags extra_usage) {
    extra_usage_ = extra_usage;
    const bool ok_r =
        BufferUtils::initialize_buffer(device, allocator, size_bytes, render(),
                                       extra_usage);
    const bool ok_u =
        BufferUtils::initialize_buffer(device, allocator, size_bytes, upload(),
                                       extra_usage);
    return ok_r && ok_u;
}

void DoubleBufferedBuffer::destroy(VkDevice device, VmaAllocator allocator) {
    BufferUtils::destroy_buffer(device, allocator, upload());
    BufferUtils::destroy_buffer(device, allocator, render());
    buffers_ = {};
    max_elements_ = {0, 0};
    upload_ = 1;
    render_ = 0;
}

bool DoubleBufferedBuffer::ensure_element_capacity(
    VkDevice device, VmaAllocator allocator, uint32_t needed, uint32_t element_size,
    uint32_t growth_step, const void* copy_src, size_t copy_bytes,
    VkBufferUsageFlags extra_usage) {
    if (needed <= max_elements_[upload_])
        return true;

    const uint32_t new_cap =
        next_grown_capacity(max_elements_[upload_], needed, growth_step);
    const VkDeviceSize new_size =
        static_cast<VkDeviceSize>(new_cap) * static_cast<VkDeviceSize>(element_size);

    const VkBufferUsageFlags usage =
        (extra_usage != 0) ? extra_usage : extra_usage_;

    if (!BufferUtils::resize_buffer(device, allocator, new_size, upload(),
                                   const_cast<void*>(copy_src), copy_bytes, usage)) {
        return false;
    }
    max_elements_[upload_] = new_cap;
    return true;
}

bool DoubleBufferedBuffer::ensure_byte_capacity(
    VkDevice device, VmaAllocator allocator, VkDeviceSize needed_bytes,
    VkDeviceSize growth_pad, const void* copy_src, size_t copy_bytes,
    VkBufferUsageFlags extra_usage) {
    if (upload().buffer != VK_NULL_HANDLE && upload().info.size >= needed_bytes)
        return true;

    const VkDeviceSize new_size = needed_bytes + growth_pad;
    const VkBufferUsageFlags usage =
        (extra_usage != 0) ? extra_usage : extra_usage_;

    return BufferUtils::resize_buffer(device, allocator, new_size, upload(),
                                     const_cast<void*>(copy_src), copy_bytes, usage);
}

void DoubleBufferedBuffer::upload_memcpy(const void* src, size_t bytes) {
    if (!upload().mapped_data || !src || bytes == 0)
        return;
    std::memcpy(upload().mapped_data, src, bytes);
}

bool DoubleBufferedBuffer::bind_render_descriptor(VkDevice device,
                                                  VkDescriptorSet set,
                                                  uint32_t binding,
                                                  VkDeviceSize used_bytes) const {
    return BufferUtils::update_descriptor(device, render(), set, used_bytes, binding);
}

} // namespace gfx
