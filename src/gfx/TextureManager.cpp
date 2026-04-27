#include "TextureManager.h"
#include <mutex>

TextureID gfx::TextureManager::get_texture_handle(const std::string &name,
                                                  const std::string &filepath) {

    std::scoped_lock lock(texture_mutex);

    // check if key is name or filepath
    const std::string *key = nullptr;
    if (name.empty())
        key = &filepath;
    else
        key = &name;

    // check if texture was already added
    auto it = texture_lookup.find(*key);
    if (it != texture_lookup.end()) {
        return it->second;
    } else {
        // not added previously, so load texture and add to upload list
        gfx::TextureInfo texture_info;
        if (name.empty())
            texture_info.name = filepath;
        else
            texture_info.name = name;

        texture_info.filepath = filepath;
        texture_cache.push_back(texture_info);

        TextureID handle(texture_cache.size() - 1);
        pending_upload.push_back(handle);
        texture_lookup[name] = handle;
        return handle;
    }
}

