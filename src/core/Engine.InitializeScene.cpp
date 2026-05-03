#include "Engine.h"
#include "GltfLoader.h"
#include "Renderer.h"
#include "SceneManager.h"
#include "gfx/TextureManager.h"
#include "nlohmann/json.hpp"
#include "tiny_gltf.h"
#include <fstream>
#include <iostream>

bool core::Engine::load_default_scene() {
    // get scene name from configuration.json
    nlohmann::json config;
    try {
        std::ifstream config_file("assets/scenes/configuration.json");
        if (!config_file.is_open()) {
            throw std::runtime_error("Failed to open configuration.json");
        }
        config = nlohmann::json::parse(config_file);
    } catch (const std::exception &e) {
        std::cerr << "Error loading configuration: " << e.what() << std::endl;
        return false;
    }

    if (!config.contains("defaultScene")) {
        std::cerr << "defaultScene not found in configuration.json"
                  << std::endl;
        return false;
    }

    return load_scene(config["defaultScene"].get<std::string>());
}

bool core::Engine::load_scene(const std::string &scene_name) {
    // get scene name from configuration.json
    nlohmann::json config;
    try {
        std::ifstream config_file("assets/scenes/configuration.json");
        if (!config_file.is_open()) {
            throw std::runtime_error("Failed to open configuration.json");
        }
        config = nlohmann::json::parse(config_file);
    } catch (const std::exception &e) {
        std::cerr << "Error loading configuration: " << e.what() << std::endl;
        return false;
    }

    // get the filename from the json config using the scene_name as a key
    if (!config.contains("scenes")) {
        std::cerr << "Scene name '" << scene_name
                  << "' not found in configuration.json" << std::endl;
        return false;
    }

    // get scenes array and find the entry where name == scene_name,
    // then get the filename from that entry
    const auto &scenes = config["scenes"];
    std::string filename;
    bool found_scene = false;
    for (const auto &scene : scenes) {
        if (scene.contains("name") && scene["name"] == scene_name) {
            if (scene.contains("filename")) {
                filename = scene["filename"].get<std::string>();
                found_scene = true;
                break;
            }
        }
    }

    if (!found_scene) {
        std::cerr << "Scene name '" << scene_name
                  << "' not found in configuration.json" << std::endl;
        return false;
    }

    // extract mesh data and create GPU buffers
    tinygltf::Model model{};
    if (!core::GltfLoader::load_model(filename, model)) {
        return false;
    }

    // create materials in the material manager and get a lookup
    std::vector<MaterialID> material_lookup =
        core::GltfLoader::extract_material_data(filename, model, renderer);

    // get meshes using lookup to store material ID on MeshData
    std::vector<MeshPrimitiveID> mesh_lookup =
        core::GltfLoader::extract_mesh_data(model, renderer);

    // calculate offsets for material lookup based on primitives
    std::vector<size_t> prim_material_offsets{};
    prim_material_offsets.reserve(model.meshes.size());
    size_t offset = 0;
    for (auto &mesh : model.meshes) {
        prim_material_offsets.push_back(offset);
        offset += mesh.primitives.size();
    }

    for (auto &node : model.nodes) {
        auto transform = core::GltfLoader::extract_node_transform(node);
        auto &mesh = model.meshes[node.mesh];
        size_t prim_i = 0;
        size_t offset = prim_material_offsets[node.mesh];
        for (auto &prim : model.meshes[node.mesh].primitives) {
            core::SceneInstance instance{};
            instance.mesh_index = mesh_lookup[prim_i + offset];
            instance.material_index = material_lookup[prim.material];
            instance.transform = transform;
            prim_i++;

            renderer.scene_manager.add_instance(instance);
        }
    }

    // At this point, the scene manager has CPU-side data for instances and
    // materials. We can call update_buffers() to upload this data to the GPU.
    renderer.scene_manager.update_buffers();
    renderer.material_manager.update_buffers();
    renderer.mesh_manager.update_buffers();
    renderer.texture_manager.upload_textures();

    // Implementation for loading scene
    return true; // Placeholder return value
}

void core::Engine::cleanup_scene() {

    // finally, clean up the scene manager
}