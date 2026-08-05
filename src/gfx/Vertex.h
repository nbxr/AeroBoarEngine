#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace gfx {

using Index = uint32_t;

// Tight, aligned vertex format for mobile VR + GPU-driven rendering.
// Stride 64: natural alignment; +RGBA8 color for glTF COLOR_0 (~14% vs old 56).
struct Vertex {
    float position[3]; //  0-11
    float normal[3];   // 12-23
    float tangent[4];  // 24-39   (xyz + handedness w)

    // UV0 + UV1 as IEEE half floats (R16G16B16A16_SFLOAT): xy=UV0, zw=UV1
    uint8_t uv[8]; // 40-47

    // glTF COLOR_0 (RGBA8 unorm). Default white when attribute missing.
    uint8_t color[4]; // 48-51

    // Skinning
    uint8_t blend_weights[4]; // 52-55
    uint8_t blend_indices[4]; // 56-59

    uint8_t _pad[4]{}; // 60-63

    static constexpr size_t stride = 64;
};

static_assert(sizeof(Vertex) == Vertex::stride,
              "Vertex layout must match stride");

} // namespace gfx
