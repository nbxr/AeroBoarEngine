#pragma once

#include "gfx/MeshData.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gfx {

struct MeshOptimizeResult {
    bool applied = false;
    uint32_t vertices_in = 0;
    uint32_t vertices_out = 0;
    uint32_t index_count = 0;
    // Old vertex index → new. Size = vertices_in when applied.
    // Unused vertices after fetch may map to ~0u.
    std::vector<uint32_t> remap;
};

struct MeshOptimizeTotals {
    uint32_t primitives = 0;
    uint32_t skipped = 0;
    uint64_t vertices_in = 0;
    uint64_t vertices_out = 0;
    uint64_t indices = 0;

    void add(const MeshOptimizeResult& r) {
        if (!r.applied) {
            ++skipped;
            return;
        }
        ++primitives;
        vertices_in += r.vertices_in;
        vertices_out += r.vertices_out;
        indices += r.index_count;
    }
};

// Load-time indexed optimize (meshoptimizer):
//   weld (binary Vertex equality + optional extra per-vertex bytes)
//   → vertex cache → overdraw → vertex fetch.
// extra is one record per *input* vertex; extra_stride 0 skips extra streams.
// weld=false still runs cache/overdraw/fetch (safe when morph extras are missing).
MeshOptimizeResult optimize_indexed_mesh(MeshData& mesh,
                                         const void* extra = nullptr,
                                         size_t extra_stride = 0,
                                         bool weld = true);

// Split into meshoptimizer meshlets (64 verts / 126 tris), rewrite the index
// buffer as concatenated global-index ranges, then vertex-fetch optimize.
// Composes onto mesh.vertex_remap when present. Returns meshlet count (0 = skip).
uint32_t build_meshlets(MeshData& mesh);

} // namespace gfx
