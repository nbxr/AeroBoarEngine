#pragma once

#include "TextureInfo.h"
#include <shared_mutex>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>
#include <vma/vk_mem_alloc.h>

namespace gfx {
class TextureManager {

  private:
    VkDevice device{VK_NULL_HANDLE};
    VmaAllocator allocator{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
    VkSampler sampler_handle{VK_NULL_HANDLE};
    VkCommandPool upload_command_pool{VK_NULL_HANDLE};
    VkQueue transfer_queue{VK_NULL_HANDLE};
    VkCommandBuffer upload_command_buffer{VK_NULL_HANDLE};
    uint32_t graphics_queue_index{VK_QUEUE_FAMILY_IGNORED};
    uint32_t transfer_queue_index{VK_QUEUE_FAMILY_IGNORED};

    std::vector<gfx::TextureInfo> texture_cache{};
    std::unordered_map<std::string, TextureID> texture_lookup{};
    std::vector<TextureID> pending_upload{};
    std::vector<TextureID> pending_queue_transition{};
    std::vector<gfx::TextureInfo> texture_cleanup{};

    // Handles that can be reused from within cpu_materials
    std::stack<TextureID> recycle_cache{};

    // Indexes to control which buffers are used for uploading
    // and which are used for rendering
    uint32_t upload = 1;
    uint32_t render = 0;

    uint32_t uploaded_count = 0;

    mutable std::shared_mutex texture_mutex;

  public:
    bool is_initialized();
    bool initialize(VkDevice device, VmaAllocator allocator,
                    VkQueue transfer_queue,
                    uint32_t graphics_queue_family_index,
                    uint32_t transfer_queue_family_index);
    TextureID get_texture_handle(const std::string &name,
                                 const std::string &filepath);
    void remove_texture(const TextureID texture_id);
    void upload_textures();
    void finalize_layout(VkBuffer command_buffer);
    void bind_descriptor(uint32_t index);
    void shutdown();

  private:
    static void load_pixel_data(std::vector<uint8_t> &pixels,
                                gfx::TextureInfo &info);
};
} // namespace gfx