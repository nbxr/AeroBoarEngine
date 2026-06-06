#include "gfx/Engine.h"
#include "gfx/BufferUtils.h"
#include "gfx/Renderer.h"
#include "gfx/TextureManager.h"
#include "nlohmann/json.hpp"
#include "scene/GltfLoader.h"
#include "scene/SceneManager.h"
#include "tiny_gltf.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <filesystem>

#include <glm/gtc/quaternion.hpp>  // for mat3_cast in pointing debug

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

    // Resolve active home path from activeSystem + home[] array
    std::string active_system;
    if (config.contains("activeSystem")) {
        active_system = config["activeSystem"].get<std::string>();
    } else {
        active_system = "Windows"; // legacy fallback
    }

    std::string home_path;
    if (config.contains("home") && config["home"].is_array()) {
        for (const auto &entry : config["home"]) {
            if (entry.contains("system") && entry["system"] == active_system &&
                entry.contains("path")) {
                home_path = entry["path"].get<std::string>();
                break;
            }
        }
    }

    if (home_path.empty()) {
        std::cerr << "No home path configured for activeSystem '" << active_system
                  << "' in configuration.json" << std::endl;
        return false;
    }

    // get the filename from the json config using the scene_name
    if (!config.contains("scenes")) {
        std::cerr << "scenes array not found in configuration.json" << std::endl;
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

    // Concatenate home path for activeSystem with the scene's relative filename
    std::filesystem::path resolved_path =
        std::filesystem::path(home_path) / filename;
    std::string resolved_filename = resolved_path.make_preferred().string();

    // extract mesh data and create GPU buffers
    tinygltf::Model model{};
    if (!scene::GltfLoader::load_model(resolved_filename, model)) {
        return false;
    }

    // create materials in the material manager and get a lookup
    std::vector<MaterialID> material_lookup =
        scene::GltfLoader::extract_material_data(resolved_filename, model, renderer);

    // Ensure at least one material exists (e.g. minimal test scenes like Cameras.gltf
    // define geometry but no materials array, and primitives may omit "material").
    // The shader treats NO_TEXTURE indices by falling back to the factor values.
    if (material_lookup.empty()) {
        gfx::Material def{};
        def.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        def.roughness = 0.5f;
        def.metallic = 0.0f;
        def.emissive = 0.0f;
        def.normalStrength = 1.0f;
        def.albedo_texture_index = gfx::Material::NO_TEXTURE;
        def.normal_texture_index = gfx::Material::NO_TEXTURE;
        def.roughness_texture_index = gfx::Material::NO_TEXTURE;
        def.emissive_texture_index = gfx::Material::NO_TEXTURE;
        def.ao_texture_index = gfx::Material::NO_TEXTURE;
        def.sampler_index = gfx::Material::NO_TEXTURE;
        def.flags = 0;
        MaterialID def_id = renderer.material_manager.create_material(def);
        material_lookup.push_back(def_id);
    }

    // get meshes using lookup to store material ID on MeshData
    std::vector<MeshPrimitiveID> mesh_lookup =
        scene::GltfLoader::extract_mesh_data(model, renderer);

    // Phase 2: extract KHR_lights_punctual lights (if any)
    renderer.lights = scene::GltfLoader::extract_light_data(model);

    // Populate initial FrameGlobals (lights + camera) into the upload side
    {
        uint32_t up = renderer.globals_upload;
        auto *dst = static_cast<gfx::FrameGlobals *>(
            renderer.frame_globals_buffer[up].mapped_data);
        if (dst) {
            memset(dst, 0, sizeof(gfx::FrameGlobals));

            // Camera (will be updated every frame in render too)
            dst->cameraPosition = glm::vec4(camera.get_position(), 1.0f);
            dst->exposure = 1.0f;

            // Initial FrameGlobals lights: prefer engine globalLight as a safe
            // default. If the scene contained KHR_lights_punctual lights, they
            // will be world-transformed later in this function (after traversal)
            // and will overwrite the light slots below via the post-traversal
            // sync block. This keeps the early init simple while making scene
            // lights the active source when present.
            if (renderer.globalLight.type == gfx::LightType::Directional &&
                glm::length(renderer.globalLight.positionOrDirection) <
                    0.001f) {
                // Initialize a nice default global sun light if not yet
                // configured
                renderer.globalLight = {
                    gfx::LightType::Directional,
                    glm::vec3(0.0f, -1.0f, 0.0f),  // direction (overhead fallback)
                    glm::vec3(1.0f, 0.98f, 0.95f), // warm sunlight color
                    1.0f                           // intensity
                };
            }

            std::vector<gfx::Light> activeLights;
            activeLights.push_back(renderer.globalLight);

            dst->lightCount =
                std::min<uint32_t>(activeLights.size(), gfx::MAX_LIGHTS);
            for (uint32_t i = 0; i < dst->lightCount; ++i) {
                const auto &L = activeLights[i];
                dst->lightDirectionsOrPositions[i] =
                    glm::vec4(L.positionOrDirection, 0.0f);
                dst->lightColors[i] = glm::vec4(L.color, L.intensity);
                dst->lightParams[i] =
                    glm::vec4(static_cast<float>(L.type), L.range,
                              L.innerConeAngle, L.outerConeAngle);
            }

            // Phase 3 IBL defaults (simple cool-ish ambient SH + no maps yet)
            // These are very rough L0 + L1 coefficients for a slightly blue
            // ambient
            dst->shCoefficients[0] = glm::vec4(0.15f, 0.18f, 0.22f, 0.0f); // L0
            // Leave higher bands at zero for now (pure ambient)
            for (int i = 1; i < 9; ++i) {
                dst->shCoefficients[i] = glm::vec4(0.0f);
            }

            dst->specularEnvMapIndex = gfx::NO_TEXTURE;
            dst->brdfLutIndex = gfx::NO_TEXTURE;
        }

        // Mirror to render side for first frame (simple for Phase 2)
        auto *renderDst = static_cast<gfx::FrameGlobals *>(
            renderer.frame_globals_buffer[renderer.globals_render].mapped_data);
        if (renderDst && dst) {
            memcpy(renderDst, dst, sizeof(gfx::FrameGlobals));
        }
    }

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
        float aspectRatio = 0.0f;   // 0 = not specified in glTF (use runtime aspect)
        float znear = 0.1f;
        float zfar = 100.0f;
        bool valid = false;
        int   gltf_camera_index = -1;
    } loaded_camera{};

    // Safe GLTF traversal:
    // - Visit the full scene graph starting from the active scene's root nodes.
    // - Accumulate world transforms for *all* nodes (meshes, cameras, lights, etc.).
    // - Camera nodes are captured (first one found in DFS order) so we can
    //   drive the view from the authored camera.
    // - Light nodes (KHR_lights_punctual) now have their world transforms captured
    //   here so we can drive lighting from the scene (see post-traversal apply below).
    // - Only nodes with meshes produce SceneInstance entries.
    // - Respect the scene graph: start from the default scene roots and
    //   accumulate world transforms via children.
    // - Guard against prim.material == -1 (default material) and out-of-range
    //   indices.
    std::vector<glm::mat4> light_world_transforms;
    if (!renderer.lights.empty()) {
        light_world_transforms.assign(renderer.lights.size(), glm::mat4(1.0f));
    }

    std::function<void(int, const glm::mat4 &, int)> add_mesh_node =
        [&](int node_idx, const glm::mat4 &parent_xform, int depth) {
            if (node_idx < 0 || node_idx >= (int)model.nodes.size())
                return;
            const auto &node = model.nodes[node_idx];
            glm::mat4 local = scene::GltfLoader::extract_node_transform(node);
            glm::mat4 world_xform = parent_xform * local;

            // Capture world transform for any light nodes so we can apply
            // position / direction from the authored node hierarchy (KHR_lights_punctual).
            if (node.light >= 0 && node.light < (int)light_world_transforms.size()) {
                light_world_transforms[node.light] = world_xform;
            }

            // Capture the first camera we encounter (minimal implementation)
            if (!loaded_camera.valid && node.camera >= 0 &&
                node.camera < (int)model.cameras.size()) {
                const auto &cam = model.cameras[node.camera];
                if (cam.type == "perspective") {
                    const auto &p = cam.perspective;
                    loaded_camera.world_transform = world_xform;
                    loaded_camera.yfov = (p.yfov > 0.0)
                                             ? static_cast<float>(p.yfov)
                                             : glm::radians(60.0f);
                    loaded_camera.aspectRatio = (p.aspectRatio > 0.0)
                                                    ? static_cast<float>(p.aspectRatio)
                                                    : 0.0f;
                    loaded_camera.znear =
                        (p.znear > 0.0) ? static_cast<float>(p.znear) : 0.1f;
                    loaded_camera.zfar =
                        (p.zfar > 0.0) ? static_cast<float>(p.zfar) : 100.0f;
                    loaded_camera.gltf_camera_index = node.camera;
                    loaded_camera.valid = true;
                }
                // Orthographic cameras are ignored in this minimal version
            }

            if (node.mesh >= 0 && node.mesh < (int)model.meshes.size()) {
                const size_t mesh_idx = static_cast<size_t>(node.mesh);
                size_t prim_i = 0;
                size_t mat_offset = prim_material_offsets[mesh_idx];

                for (const auto &prim : model.meshes[mesh_idx].primitives) {
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

                    // Store world-space AABB for this primitive (used for
                    // camera framing, future culling, etc.)
                    core::AABB local =
                        renderer.mesh_manager.get_primitive_local_aabb(
                            instance.mesh_index);
                    instance.local_aabb = local.transformed(world_xform);

                    renderer.scene_manager.add_instance(instance);
                }
            }

            // Recurse into children with accumulated transform
            for (int child : node.children) {
                add_mesh_node(child, world_xform, depth + 1);
            }
        };

    // Choose the active scene and walk from its root nodes
    int active_scene = (model.defaultScene >= 0) ? model.defaultScene : 0;
    if (!model.scenes.empty() && active_scene < (int)model.scenes.size()) {
        for (int root_node : model.scenes[active_scene].nodes) {
            add_mesh_node(root_node, glm::mat4(1.0f), 0);
        }
    } else {
        // Fallback for malformed files: walk any node that has a mesh (still
        // safe)
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            if (model.nodes[i].mesh >= 0) {
                add_mesh_node(static_cast<int>(i), glm::mat4(1.0f), 0);
            }
        }
    }

    // Phase 2 lighting: apply collected world transforms to the extracted lights.
    // This makes KHR_lights_punctual lights appear in the correct world-space
    // locations/orientations authored in the DCC tool. Directional lights store
    // the engine's L vector (to-light) = node's local +Z in world (opposite the
    // emission direction per KHR spec: "emit light in the direction of the local -z axis").
    for (size_t i = 0; i < renderer.lights.size() && i < light_world_transforms.size(); ++i) {
        const glm::mat4& w = light_world_transforms[i];
        scene::GltfLoader::apply_world_transform_to_light(renderer.lights[i], w);
    }

    // Upload CPU data (populated by GltfLoader) into the persistently-mapped
    // GPU buffers on the "upload" side of each double-buffered manager.
    renderer.scene_manager.update_buffers();
    renderer.material_manager.update_buffers();
    renderer.mesh_manager.update_buffers();
    renderer.texture_manager.upload_textures();

    // Commit: flip the buffers so the side we just wrote becomes the render
    // side, then write the actual VkBuffer handles + ranges into the bindless
    // descriptor set (which is now allocated). This makes scene data visible
    // to shaders for the upcoming render pass work.
    // Binding indices: 1=instances, 2=materials, 3=meshmeta, 4=verts,
    // 5=indices, 6=textures (textures must be the highest binding number
    // because of VARIABLE count).
    renderer.scene_manager.toggle_buffers();
    renderer.material_manager.toggle_buffers();
    renderer.mesh_manager.toggle_buffers();

    renderer.scene_manager.bind_descriptor(1);    // SceneInstance (transforms)
    renderer.material_manager.bind_descriptor(2); // Materials
    renderer.mesh_manager.bind_descriptor(
        3, 4, 5); // Mesh meta + vertex + index SSBOs
    renderer.texture_manager.bind_descriptor(
        6); // Bindless textures (must be last binding)

    // Phase 2 lighting: bind per-frame globals UBO (binding 0)
    {
        uint32_t renderIdx = renderer.globals_render;
        gfx::BufferUtils::update_descriptor(
            renderer.vk.device.device, renderer.frame_globals_buffer[renderIdx],
            renderer.vk.bindless_descriptor_set, sizeof(gfx::FrameGlobals), 0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    }

    // glTF camera support: if the scene contains a camera node, use its
    // world transform + projection parameters for the initial view.
    // Falls back to AABB framing when no camera is present.
    if (loaded_camera.valid) {
        camera.set_from_camera_node(loaded_camera.world_transform,
                                    loaded_camera.yfov,
                                    loaded_camera.znear,
                                    loaded_camera.zfar,
                                    loaded_camera.aspectRatio);
    } else {
        // Fallback: AABB-based framing
        auto [center, radius] =
            renderer.scene_manager.get_first_instance_framing_sphere();
        camera.frame(center, radius);
    }

    // Reset mouse input tracking after placing the camera at its final loaded pose.
    // The delta guards in InputManager protect the initial view from being
    // immediately disturbed by any pending OS cursor position.
    camera.reset_mouse_state();

    // Phase 2 lighting: if the scene had KHR_lights_punctual lights and we
    // successfully applied their node world transforms above, push them into
    // the FrameGlobals UBO buffers now (overwriting the globalLight fallback
    // that was written in the early init block). This makes the scene's light(s)
    // the active source seen by pbr.frag.
    if (!renderer.lights.empty()) {
        const uint32_t n = std::min<uint32_t>(renderer.lights.size(), gfx::MAX_LIGHTS);
        for (uint32_t side = 0; side < 2; ++side) {
            auto *dst = static_cast<gfx::FrameGlobals *>(
                renderer.frame_globals_buffer[side].mapped_data);
            if (dst) {
                memset(dst, 0, sizeof(gfx::FrameGlobals));
                dst->lightCount = n;
                float maxI = 0.0f;
                for (uint32_t i = 0; i < gfx::MAX_LIGHTS; ++i) {
                    if (i < n) {
                        const auto &L = renderer.lights[i];
                        dst->lightDirectionsOrPositions[i] =
                            glm::vec4(L.positionOrDirection, 0.0f);
                        dst->lightColors[i] = glm::vec4(L.color, L.intensity);
                        dst->lightParams[i] =
                            glm::vec4(static_cast<float>(L.type), L.range,
                                      L.innerConeAngle, L.outerConeAngle);
                        if (L.intensity > maxI) maxI = L.intensity;
                    } else {
                        // Explicitly zero unused slots (see per-frame path for rationale).
                        dst->lightDirectionsOrPositions[i] = glm::vec4(0.0f);
                        dst->lightColors[i] = glm::vec4(0.0f);
                        dst->lightParams[i] = glm::vec4(0.0f);
                    }
                }
                // Choose a display exposure so the photometric intensities
                // (e.g. 54k from Blender Power=1000 export via KHR_lights_punctual)
                // produce visible contributions on the model. The shader multiplies
                // the direct (diff+spec) term by this value; a cheap compressor in
                // the frag prevents hard clipping.
                dst->exposure = (maxI > 10.0f) ? (20.0f / maxI) : 1.0f;
                // Leave higher slots (if any) as they were; lightCount gates them.
            }
        }

        // Re-bind the globals descriptor now that we have written the final
        // scene light data + chosen exposure into the buffers. The initial
        // bind happened earlier (with the temporary fallback); this ensures
        // the descriptor sees the authoritative scene light values.
        {
            uint32_t renderIdx = renderer.globals_render;
            gfx::BufferUtils::update_descriptor(
                renderer.vk.device.device,
                renderer.frame_globals_buffer[renderIdx],
                renderer.vk.bindless_descriptor_set,
                sizeof(gfx::FrameGlobals),
                0,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        }
    }

    // TODO: add proper memory barriers / vkFlushMappedMemoryRanges for the
    // buffer uploads if running on non-coherent memory (Quest 3). For desktop
    // dev with persistently mapped + sequential write the data is usually
    // visible after the next submit that uses the descriptors.

    return true;
}

void gfx::Engine::cleanup_scene() {
    // Phase 2 lighting: clear lights and reset globals indices so reloads are
    // safe
    renderer.lights.clear();

    // Reset globals double-buffer indices (simple safety)
    renderer.globals_upload = 1;
    renderer.globals_render = 0;

    // TODO: In a fuller implementation we would also destroy/recreate the
    // globals buffers here if supporting multiple scene loads without full
    // engine restart. For now the buffers live for the lifetime of the Engine.

    // finally, clean up the scene manager
    renderer.scene_manager.shutdown(); // existing (mostly empty) call
}