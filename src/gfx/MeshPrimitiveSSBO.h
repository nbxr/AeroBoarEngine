#pragma once
#include <cstdint>
namespace gfx {
struct MeshPrimitiveSSBO {
    uint32_t vertex_offset;
    uint32_t vertex_count;
    uint32_t index_offset;
    uint32_t index_count;
};
}; // namespace gfx