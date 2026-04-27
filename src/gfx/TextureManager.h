#pragma once

#include "TextureInfo.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <shared_mutex>

namespace gfx {
class TextureManager {
  private:
    std::vector<gfx::TextureInfo> texture_cache{};
    std::unordered_map<std::string, TextureID> texture_lookup{};
    std::vector<TextureID> pending_upload{};
    mutable std::shared_mutex texture_mutex;

  public:
    // Texture staging
    TextureID get_texture_handle(const std::string &name,
                                 const std::string &filepath);

};
} // namespace gfx