#pragma once

#include <string>

// forward declaration of tinygltf::Model to avoid including 
// the entire tinygltf header
namespace tinygltf {
class Model;
};

namespace core {
class GltfLoader {
  public:
    static bool load_model(const std::string &filename, tinygltf::Model &model);
};
}; // namespace core