#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace gfx {

using Index = uint32_t;

// Tight, aligned vertex format for mobile VR + GPU-driven rendering (mesh
// shaders / indirect)
struct Vertex {
    float position[3]; //  0-11
    float normal[3];   // 12-23
    float tangent[4];  // 24-39   (xyz + handedness w)

    // UV0 + UV1 (each packed as two uint16_t)
    uint8_t uv[8]; // 40-47

    // Skinning (kept for future animated meshes)
    uint8_t blend_weights[4]; // 48-51
    uint8_t blend_indices[4]; // 52-55

    // Optional: Vertex color (RGBA8) - uncomment when needed
    // uint8_t color[4];       // would push stride to 60 → pad to 64

    static constexpr size_t stride = 56;
};

static_assert(sizeof(Vertex) == Vertex::stride,
              "Vertex layout must match stride");

} // namespace gfx