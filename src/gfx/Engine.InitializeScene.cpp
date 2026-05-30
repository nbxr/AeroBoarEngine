#include "gfx/Engine.h"
#include "scene/GltfLoader.h"
#include "gfx/Renderer.h"
#include "scene/SceneManager.h"
#include "gfx/TextureManager.h"
#include "nlohmann/json.hpp"
#include "tiny_gltf.h"
#include <fstream>
#include <functional>
#include <iostream>

bool gfx::Engine::load_default_scene() {
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

bool gfx::Engine::load_scene(const std::string &scene_name) {
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
    if (!scene::GltfLoader::load_model(filename, model)) {
        return false;
    }

    // create materials in the material manager and get a lookup
    std::vector<MaterialID> material_lookup =
        scene::GltfLoader::extract_material_data(filename, model, renderer);

    // get meshes using lookup to store material ID on MeshData
    std::vector<MeshPrimitiveID> mesh_lookup =
        scene::GltfLoader::extract_mesh_data(model, renderer);

    // calculate offsets for material lookup based on primitives
    std::vector<size_t> prim_material_offsets{};
    prim_material_offsets.reserve(model.meshes.size());
    size_t offset = 0;
    for (auto &mesh : model.meshes) {
        prim_material_offsets.push_back(offset);
        offset += mesh.primitives.size();
    }

    // Minimal glTF camera capture (only perspective for now)
    struct LoadedCameraInfo {
        glm::mat4 world_transform{1.0f};
        float yfov = glm::radians(60.0f);
        float znear = 0.1f;
        float zfar = 100.0f;
        bool valid = false;
    };
    LoadedCameraInfo loaded_camera;

    // Safe GLTF traversal:
    // - Only process nodes that actually reference a mesh (skip cameras, lights, groups, etc.)
    // - Respect the scene graph: start from the default scene roots and accumulate world transforms via children.
    // - Guard against prim.material == -1 (default material) and out-of-range indices.
    std::function<void(int, const glm::mat4&)> add_mesh_node = [&](int node_idx, const glm::mat4& parent_xform) {
        if (node_idx < 0 || node_idx >= (int)model.nodes.size())
            return;
        const auto& node = model.nodes[node_idx];
        glm::mat4 local = scene::GltfLoader::extract_node_transform(node);
        glm::mat4 world_xform = parent_xform * local;

        // Capture the first camera we encounter (minimal implementation)
        if (!loaded_camera.valid && node.camera >= 0 && node.camera < (int)model.cameras.size()) {
            const auto& cam = model.cameras[node.camera];
            if (cam.type == "perspective") {
                const auto& p = cam.perspective;
                loaded_camera.world_transform = world_xform;
                loaded_camera.yfov = (p.yfov > 0.0) ? static_cast<float>(p.yfov) : glm::radians(60.0f);
                loaded_camera.znear = (p.znear > 0.0) ? static_cast<float>(p.znear) : 0.1f;
                loaded_camera.zfar = (p.zfar > 0.0) ? static_cast<float>(p.zfar) : 100.0f;
                loaded_camera.valid = true;
            }
            // Orthographic cameras are ignored in this minimal version
        }

        if (node.mesh >= 0 && node.mesh < (int)model.meshes.size()) {
            const size_t mesh_idx = static_cast<size_t>(node.mesh);
            size_t prim_i = 0;
            size_t mat_offset = prim_material_offsets[mesh_idx];

            for (const auto& prim : model.meshes[mesh_idx].primitives) {
                if (prim_i + mat_offset >= mesh_lookup.size())
                    break;

                scene::SceneInstance instance{};
                instance.mesh_index = mesh_lookup[prim_i + mat_offset];

                int mat_idx = prim.material;
                if (mat_idx < 0 || mat_idx >= (int)material_lookup.size()) {
                    mat_idx = 0; // fallback to first material (or default)
                }
                instance.material_index = material_lookup[mat_idx];
                instance.transform = world_xform;
                prim_i++;

                // Store world-space AABB for this primitive (used for camera framing, future culling, etc.)
                core::AABB local = renderer.mesh_manager.get_primitive_local_aabb(instance.mesh_index);
                instance.local_aabb = local.transformed(world_xform);

                renderer.scene_manager.add_instance(instance);
            }
        }

        // Recurse into children with accumulated transform
        for (int child : node.children) {
            add_mesh_node(child, world_xform);
        }
    };

    // Choose the active scene and walk from its root nodes
    int active_scene = (model.defaultScene >= 0) ? model.defaultScene : 0;
    if (!model.scenes.empty() && active_scene < (int)model.scenes.size()) {
        for (int root_node : model.scenes[active_scene].nodes) {
            add_mesh_node(root_node, glm::mat4(1.0f));
        }
    } else {
        // Fallback for malformed files: walk any node that has a mesh (still safe)
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (model.nodes[i].mesh >= 0) {
                add_mesh_node(static_cast<int>(i), glm::mat4(1.0f));
            }
        }
    }

    // Upload CPU data (populated by GltfLoader) into the persistently-mapped
    // GPU buffers on the "upload" side of each double-buffered manager.
    renderer.scene_manager.update_buffers();
    renderer.material_manager.update_buffers();
    renderer.mesh_manager.update_buffers();
    renderer.texture_manager.upload_textures();

    // Commit: flip the buffers so the side we just wrote becomes the render side,
    // then write the actual VkBuffer handles + ranges into the bindless
    // descriptor set (which is now allocated). This makes scene data visible
    // to shaders for the upcoming render pass work.
    // Binding indices: 1=instances, 2=materials, 3=meshmeta, 4=verts, 5=indices, 6=textures
    // (textures must be the highest binding number because of VARIABLE count).
    renderer.scene_manager.toggle_buffers();
    renderer.material_manager.toggle_buffers();
    renderer.mesh_manager.toggle_buffers();

    renderer.scene_manager.bind_descriptor(1);      // SceneInstance (transforms)
    renderer.material_manager.bind_descriptor(2);   // Materials
    renderer.mesh_manager.bind_descriptor(3, 4, 5); // Mesh meta + vertex + index SSBOs
    renderer.texture_manager.bind_descriptor(6);    // Bindless textures (must be last binding)

    // Minimal glTF camera support: use author camera if present, otherwise fall back to AABB framing
    if (loaded_camera.valid) {
        camera.set_from_camera_node(loaded_camera.world_transform,
                                    loaded_camera.yfov,
                                    loaded_camera.znear,
                                    loaded_camera.zfar);
        printf("[Camera] Using glTF camera node (perspective)\n");
    } else {
        // Fallback: AABB-based framing (existing behavior)
        auto [center, radius] = renderer.scene_manager.get_first_instance_framing_sphere();
        camera.frame(center, radius);
        printf("[Camera] Initial frame using AABB: center=(%.2f, %.2f, %.2f) radius=%.2f\n",
               center.x, center.y, center.z, radius);
    }

    // TODO: add proper memory barriers / vkFlushMappedMemoryRanges for the
    // buffer uploads if running on non-coherent memory (Quest 3). For desktop
    // dev with persistently mapped + sequential write the data is usually
    // visible after the next submit that uses the descriptors.

    return true;
}

void gfx::Engine::cleanup_scene() {

    // finally, clean up the scene manager
}