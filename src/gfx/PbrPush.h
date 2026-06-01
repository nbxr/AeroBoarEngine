#pragma once

#include "glm/glm.hpp"

namespace gfx {
struct PbrPush {
    glm::mat4 viewProj;
    glm::mat4 model;
    glm::uvec4 extra;    // x = materialIndex (for bindless material SSBO)
    glm::vec4 cameraPos; // legacy (camera position now lives in FrameGlobals
                         // UBO for lighting)
};
} // namespace gfx