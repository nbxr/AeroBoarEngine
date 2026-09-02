#pragma once

#include "gfx/Renderer.h"
#include "gfx/MaterialManager.h"
#include "gfx/MeshData.h"
#include "gfx/Light.h"

// GLM configuration for Vulkan
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include "scene/TransformManager.h"
#include <glm/glm.hpp>
#include <string>

// forward declaration of tinygltf::Model to avoid including
// the entire tinygltf header
namespace tinygltf {
class Model;
class Node;
}; // namespace tinygltf

namespace scene {
class GltfLoader {
  public:
    static bool load_model(const std::string &filename, tinygltf::Model &model);
    static std::vector<gfx::MaterialID>
    extract_material_data(const std::string &filename,
                          const tinygltf::Model &model,
                          gfx::Renderer &renderer);
    static std::vector<gfx::MeshPrimitiveID>
    extract_mesh_data(const tinygltf::Model &model, gfx::Renderer &renderer,
                      bool optimize_meshes = true);
    static glm::mat4 extract_node_transform(const tinygltf::Node &node);
    // Decomposed TRS for animation (matrix nodes best-effort decompose).
    static LocalTrs extract_node_trs(const tinygltf::Node &node);

    // Phase 2 lighting: extract KHR_lights_punctual lights (if present)
    static std::vector<gfx::Light> extract_light_data(const tinygltf::Model &model);

    // Apply node world transform (KHR_lights_punctual):
    // directional → direction = to-light (+Z); point → position; spot → position + emission (−Z).
    static void apply_world_transform_to_light(gfx::Light& light, const glm::mat4& worldTransform);

  private:
};
} // namespace scene