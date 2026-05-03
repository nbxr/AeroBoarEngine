#pragma once
#include "core/AllocatedImage.h"
#include "core/Handle.h"
#include <string>
#include <vulkan/vulkan.h>

namespace gfx {
// Texture staging

struct TextureInfo {
    std::string name;
    std::string filepath;

    // GPU image data - allocated via VMA
    core::AllocatedImage gpu_image{}; // <-- Add this

    // Optional: metadata for bindless
    uint32_t width{0};
    uint32_t height{0};
    uint32_t channels{0};
    uint32_t mip_levels{1};
    VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
};

}; // namespace gfx

using TextureID = Handle<gfx::TextureInfo>;