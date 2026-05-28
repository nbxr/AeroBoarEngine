#pragma once

#include "gfx/Renderer.h"
#include "gfx/MaterialManager.h"
#include "gfx/MeshData.h"
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
    extract_mesh_data(const tinygltf::Model &model, gfx::Renderer &renderer);
    static glm::mat4 extract_node_transform(const tinygltf::Node &node);

  private:
    static std::vector<double> value_or_ident(const std::vector<double> &value,
                                              const size_t len);
};
} // namespace scene