#pragma once
#include "gfx/AllocatedImage.h"
#include "core/Handle.h"
#include <cstdint>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace gfx {
// Texture staging

struct TextureInfo {
    std::string name;
    std::string filepath;

    // GPU image data - allocated via VMA
    AllocatedImage gpu_image{};

    // Optional: metadata for bindless
    uint32_t width{0};
    uint32_t height{0};
    uint32_t channels{0};
    uint32_t mip_levels{1};
    VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};

    // If non-empty, upload uses these RGBA8 pixels instead of loading filepath.
    // Used for .glb embedded images (bufferView / tinygltf-decoded).
    std::vector<uint8_t> cpu_pixels{};
};

}; // namespace gfx

// Texture ID (in gfx namespace)
namespace gfx {
using TextureID = core::Handle<TextureInfo>;
} // namespace gfx