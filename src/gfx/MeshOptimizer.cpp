#include "gfx/MeshOptimizer.h"
#include "core/AABB.h"

#include <meshoptimizer.h>

#include <glm/glm.hpp>
#include <vector>

namespace gfx {
namespace {

bool indices_in_range(const std::vector<Index>& indices, uint32_t vertex_count) {
    for (Index i : indices) {
        if (i >= vertex_count)
            return false;
    }
    return true;
}

void recompute_aabb(MeshData& mesh) {
    core::AABB aabb{};
    for (const Vertex& v : mesh.vertices) {
        aabb.expand(glm::vec3(v.position[0], v.position[1], v.position[2]));
    }
    mesh.local_aabb = aabb;
}

} // namespace

MeshOptimizeResult optimize_indexed_mesh(MeshData& mesh, const void* extra,
                                         size_t extra_stride, bool weld) {
    MeshOptimizeResult result{};
    const uint32_t vertex_count = static_cast<uint32_t>(mesh.vertices.size());
    const uint32_t index_count = static_cast<uint32_t>(mesh.indices.size());
    result.vertices_in = vertex_count;
    result.index_count = index_count;

    if (vertex_count < 3 || index_count < 3 || (index_count % 3) != 0)
        return result;
    if (!indices_in_range(mesh.indices, vertex_count))
        return result;

    const Vertex* vertices = mesh.vertices.data();
    const unsigned int* indices =
        reinterpret_cast<const unsigned int*>(mesh.indices.data());

    std::vector<unsigned int> weld_remap(vertex_count);
    size_t unique = 0;
    if (weld) {
        if (extra != nullptr && extra_stride > 0) {
            meshopt_Stream streams[2];
            streams[0].data = vertices;
            streams[0].size = sizeof(Vertex);
            streams[0].stride = sizeof(Vertex);
            streams[1].data = extra;
            streams[1].size = extra_stride;
            streams[1].stride = extra_stride;
            unique = meshopt_generateVertexRemapMulti(
                weld_remap.data(), indices, index_count, vertex_count, streams,
                2);
        } else {
            unique = meshopt_generateVertexRemap(weld_remap.data(), indices,
                                                 index_count, vertices,
                                                 vertex_count, sizeof(Vertex));
        }
    } else {
        unique = vertex_count;
        for (uint32_t i = 0; i < vertex_count; ++i)
            weld_remap[i] = i;
    }
    if (unique == 0)
        return result;

    std::vector<Vertex> welded(unique);
    std::vector<unsigned int> opt_idx(index_count);
    meshopt_remapVertexBuffer(welded.data(), vertices, vertex_count,
                              sizeof(Vertex), weld_remap.data());
    meshopt_remapIndexBuffer(opt_idx.data(), indices, index_count,
                             weld_remap.data());

    meshopt_optimizeVertexCache(opt_idx.data(), opt_idx.data(), index_count,
                                unique);
    meshopt_optimizeOverdraw(opt_idx.data(), opt_idx.data(), index_count,
                             welded[0].position, unique, sizeof(Vertex), 1.05f);

    std::vector<unsigned int> fetch_remap(unique);
    const size_t fetched = meshopt_optimizeVertexFetchRemap(
        fetch_remap.data(), opt_idx.data(), index_count, unique);
    if (fetched == 0)
        return result;

    std::vector<Vertex> fetched_verts(fetched);
    meshopt_remapVertexBuffer(fetched_verts.data(), welded.data(), unique,
                              sizeof(Vertex), fetch_remap.data());
    meshopt_remapIndexBuffer(opt_idx.data(), opt_idx.data(), index_count,
                             fetch_remap.data());

    result.remap.resize(vertex_count);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        const unsigned int w = weld_remap[i];
        result.remap[i] =
            (w < unique) ? static_cast<uint32_t>(fetch_remap[w]) : ~0u;
    }

    mesh.vertices = std::move(fetched_verts);
    mesh.indices.assign(opt_idx.begin(), opt_idx.end());
    recompute_aabb(mesh);

    result.applied = true;
    result.vertices_out = static_cast<uint32_t>(mesh.vertices.size());
    result.index_count = static_cast<uint32_t>(mesh.indices.size());
    return result;
}

uint32_t build_meshlets(MeshData& mesh) {
    mesh.meshlets.clear();
    const uint32_t vertex_count = static_cast<uint32_t>(mesh.vertices.size());
    const uint32_t index_count = static_cast<uint32_t>(mesh.indices.size());
    if (vertex_count < 3 || index_count < 3 || (index_count % 3) != 0)
        return 0;
    if (!indices_in_range(mesh.indices, vertex_count))
        return 0;

    constexpr size_t kMaxVerts = 64;
    constexpr size_t kMaxTris = 126;
    constexpr float kConeWeight = 0.5f;

    const size_t bound =
        meshopt_buildMeshletsBound(index_count, kMaxVerts, kMaxTris);
    std::vector<meshopt_Meshlet> built(bound);
    std::vector<unsigned int> mverts(index_count);
    std::vector<unsigned char> mtris(index_count);

    const size_t nmeshlets = meshopt_buildMeshlets(
        built.data(), mverts.data(), mtris.data(), mesh.indices.data(),
        index_count, mesh.vertices[0].position, vertex_count, sizeof(Vertex),
        kMaxVerts, kMaxTris, kConeWeight);
    if (nmeshlets == 0)
        return 0;

    const meshopt_Meshlet& last = built[nmeshlets - 1];
    mverts.resize(last.vertex_offset + last.vertex_count);
    mtris.resize(last.triangle_offset + last.triangle_count * 3);
    built.resize(nmeshlets);

    std::vector<Index> packed;
    packed.reserve(index_count);
    mesh.meshlets.reserve(nmeshlets);

    for (const meshopt_Meshlet& ml : built) {
        meshopt_optimizeMeshlet(&mverts[ml.vertex_offset],
                                &mtris[ml.triangle_offset], ml.triangle_count,
                                ml.vertex_count);
        const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
            &mverts[ml.vertex_offset], &mtris[ml.triangle_offset],
            ml.triangle_count, mesh.vertices[0].position, vertex_count,
            sizeof(Vertex));

        MeshletDesc desc{};
        desc.first_index = static_cast<uint32_t>(packed.size());
        desc.index_count = ml.triangle_count * 3u;
        desc.cone_apex_cutoff = glm::vec4(bounds.cone_apex[0], bounds.cone_apex[1],
                                          bounds.cone_apex[2], bounds.cone_cutoff);
        desc.cone_axis_radius = glm::vec4(bounds.cone_axis[0], bounds.cone_axis[1],
                                          bounds.cone_axis[2], bounds.radius);
        desc.sphere_center = glm::vec4(bounds.center[0], bounds.center[1],
                                       bounds.center[2], 0.0f);

        const unsigned char* tri = &mtris[ml.triangle_offset];
        for (unsigned int t = 0; t < ml.triangle_count * 3u; ++t) {
            packed.push_back(mverts[ml.vertex_offset + tri[t]]);
        }
        mesh.meshlets.push_back(desc);
    }

    mesh.indices = std::move(packed);

    std::vector<unsigned int> fetch_remap(vertex_count);
    const size_t fetched = meshopt_optimizeVertexFetchRemap(
        fetch_remap.data(), mesh.indices.data(), mesh.indices.size(),
        vertex_count);
    if (fetched == 0) {
        mesh.meshlets.clear();
        return 0;
    }
    std::vector<Vertex> fetched_verts(fetched);
    std::vector<unsigned int> fetched_idx(mesh.indices.size());
    meshopt_remapVertexBuffer(fetched_verts.data(), mesh.vertices.data(),
                              vertex_count, sizeof(Vertex), fetch_remap.data());
    meshopt_remapIndexBuffer(fetched_idx.data(), mesh.indices.data(),
                             mesh.indices.size(), fetch_remap.data());
    mesh.vertices = std::move(fetched_verts);
    mesh.indices.assign(fetched_idx.begin(), fetched_idx.end());

    if (!mesh.vertex_remap.empty()) {
        for (uint32_t& dst : mesh.vertex_remap) {
            if (dst < vertex_count)
                dst = fetch_remap[dst];
            else
                dst = ~0u;
        }
    }
    recompute_aabb(mesh);
    return static_cast<uint32_t>(mesh.meshlets.size());
}

} // namespace gfx
