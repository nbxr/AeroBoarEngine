#pragma once
#include <string>
#include <vulkan/vulkan.h>
#include "core/Handle.h"

namespace gfx {
// Texture staging

struct TextureInfo {
    std::string name;
    std::string filepath;
};

}; // namespace gfx

using TextureID = Handle<gfx::TextureInfo>;