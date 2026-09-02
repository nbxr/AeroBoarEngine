#pragma once

#include "gfx/MeshManager.h"
#include "gfx/Vertex.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace tinygltf {
class Model;
struct Primitive;
}

namespace scene {

constexpr uint32_t kInvalidMorph = ~0u;

// Per-vertex extra bytes for meshopt weld so morph deltas stay unique.
// Returns false if targets are missing or vertex counts mismatch.
bool pack_morph_weld_bytes(const tinygltf::Model& model,
                           const tinygltf::Primitive& primitive,
                           size_t vertex_count, std::vector<uint8_t>& extra,
                           size_t& extra_stride);

// One skinned morph mesh instance (typically one glTF mesh / node).
// CPU-blends POSITION (+ NORMAL when present) and patches MeshManager vertices.
struct MorphInstance {
    uint32_t mesh_primitive_index = ~0u; // MeshManager primitive id
    uint32_t vertex_count = 0;
    uint32_t target_count = 0;
    uint32_t gltf_node_index = ~0u;

    std::vector<glm::vec3> base_positions;
    std::vector<glm::vec3> base_normals; // empty if no normals
    // Packed [target * vertex_count + v]
    std::vector<glm::vec3> target_positions;
    std::vector<glm::vec3> target_normals; // empty or same layout as positions

    std::vector<float> weights; // size = target_count
    bool dirty = true;
};

// glTF morph targets (blend shapes). Animated via animation path "weights".
class MorphSystem {
  public:
    void clear();

    // After mesh_lookup is built (same order as model.meshes primitives).
    // mesh_lookup[i] = MeshManager id for the i-th extracted primitive.
    // Returns number of morph instances.
    uint32_t load_from_gltf(const tinygltf::Model& model,
                            const std::vector<uint32_t>& mesh_lookup);

    // Apply MeshData.vertex_remap so blend-shape arrays match optimized verts.
    void apply_optimize_remap(gfx::MeshManager& meshes);

    // Associate morph instances with glTF nodes that reference their mesh.
    void bind_nodes(const tinygltf::Model& model);

    [[nodiscard]] uint32_t instance_count() const {
        return static_cast<uint32_t>(instances_.size());
    }
    [[nodiscard]] bool has_morphs() const { return !instances_.empty(); }

    // glTF node → morph instance (kInvalidMorph if none).
    [[nodiscard]] uint32_t morph_for_node(uint32_t gltf_node) const;

    void set_weights(uint32_t morph_index, const float* weights, uint32_t count);
    [[nodiscard]] const std::vector<float>& weights(uint32_t morph_index) const;

    // Blend dirty instances into MeshManager GPU vertex buffers (both FIF sides).
    // Call after animation sampling each frame.
    void apply(gfx::MeshManager& meshes);

  private:
    std::vector<MorphInstance> instances_{};
    std::vector<uint32_t> gltf_node_to_morph_{}; // size = node count
};

} // namespace scene
