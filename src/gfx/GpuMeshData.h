#pragma once

#include "core/AABB.h"

namespace gfx {

/**
 * @brief Describes the geometry data for a single mesh.
 * Matches the GPU SSBO layout for mesh data.
 */
struct GpuMeshData {
    core::AABB aabb;         // Axis-Aligned Bounding Box for the mesh  - 32 bytes
    uint32_t vertex_offset;  // Index into the global Vertex Buffer     - 4 bytes
    uint32_t index_offset;   // Index into the global Index Buffer      - 4 bytes
    uint32_t vertex_count;   // Number of vertices in this mesh         - 4 bytes
    uint32_t index_count;    // Number of indices in this mesh          - 4 bytes
    uint32_t vertex_stride;  // Stride between vertices in bytes        - 4 bytes
    uint32_t padding;        // Alignment padding                       - 4 bytes
};

} // namespace gfx