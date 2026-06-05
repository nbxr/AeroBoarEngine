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
        printf("[GLTF] No materials in file; injected default white material for geometry.\n");
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
    // Recursive traversal that also dumps the full hierarchy for debugging.
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

            // === FULL HIERARCHY TRANSFORM DUMP (user request for diagnosis) ===
            {
                std::string indent(depth * 2, ' ');
                printf("%s[NODE] idx=%d name='%s' mesh=%d cam=%d light=%d\n",
                       indent.c_str(), node_idx, node.name.c_str(),
                       node.mesh, node.camera, node.light);

                // Local transform
                if (node.matrix.size() == 16) {
                    printf("%s  local: [matrix] ", indent.c_str());
                    for (size_t i = 0; i < 16; ++i) printf("%.3f ", node.matrix[i]);
                    printf("\n");
                } else {
                    printf("%s  local: has TRS (see tinygltf node for raw values)\n", indent.c_str());
                }

                // World transform summary (most important for debugging camera vs model)
                glm::vec3 wpos = glm::vec3(world_xform[3]);
                glm::mat3 wrot = glm::mat3(world_xform);
                glm::vec3 wr = glm::normalize(wrot[0]);
                glm::vec3 wu = glm::normalize(wrot[1]);
                glm::vec3 wlook = -glm::normalize(wrot[2]); // consistent with camera convention
                printf("%s  world_pos=(%.4f,%.4f,%.4f)  right=(%.3f %.3f %.3f) up=(%.3f %.3f %.3f) look=(%.3f %.3f %.3f)\n",
                       indent.c_str(), wpos.x, wpos.y, wpos.z,
                       wr.x, wr.y, wr.z, wu.x, wu.y, wu.z, wlook.x, wlook.y, wlook.z);
            }
            // === END FULL HIERARCHY DUMP ===

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
        auto& L = renderer.lights[i];
        glm::mat3 rot = glm::mat3(w);
        if (L.type == gfx::LightType::Directional) {
            L.positionOrDirection = glm::normalize(rot[2]);  // local +Z in world
        } else {
            // Point / Spot position is the node's world translation (spec: scale/rot ignored for pos)
            L.positionOrDirection = glm::vec3(w[3]);
        }

        // Rich diagnostic (temporary, like the camera dumps) so we can see exactly
        // what values reach the shader for the scene light(s).
        const char* typeName = (L.type == gfx::LightType::Directional) ? "dir" :
                               (L.type == gfx::LightType::Point) ? "point" : "spot";
        printf("[LIGHT-TRANSFORM] idx=%zu type=%s posOrDir=(%.4f,%.4f,%.4f) color=(%.3f,%.3f,%.3f) int=%.3f range=%.3f inner=%.3f outer=%.3f\n",
               i, typeName,
               L.positionOrDirection.x, L.positionOrDirection.y, L.positionOrDirection.z,
               L.color.r, L.color.g, L.color.b, L.intensity, L.range,
               L.innerConeAngle, L.outerConeAngle);
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
    // This now correctly reproduces the authored camera pose from Blender
    // (and other DCC tools) because we directly use the node's rotation matrix.
    // Falls back to AABB framing when no camera is present.
    if (loaded_camera.valid) {
        camera.set_from_camera_node(loaded_camera.world_transform,
                                    loaded_camera.yfov,
                                    loaded_camera.znear,
                                    loaded_camera.zfar,
                                    loaded_camera.aspectRatio);
        printf("[Camera] Using glTF camera node (perspective)\n");
    } else {
        // Fallback: AABB-based framing (existing behavior)
        auto [center, radius] =
            renderer.scene_manager.get_first_instance_framing_sphere();
        camera.frame(center, radius);
        printf("[Camera] Initial frame using AABB: center=(%.2f, %.2f, %.2f) "
               "radius=%.2f\n",
               center.x, center.y, center.z, radius);
    }

    // Critical: reset mouse input tracking immediately after we have placed the
    // camera at its authored (or framed) pose.  Without this, the first call to
    // camera.update() in the main loop consumes a large mouse delta from the
    // OS cursor position at window creation time, instantly rotating the camera
    // away from the pose we just set.  This was the root cause of "I have to
    // rotate to the right to find the helmet" even though the authored pose
    // and hierarchy were correct.
    camera.reset_mouse_state();

    // === INITIAL CAMERA STATE (very important for "offset from load" tracking) ===
    {
        glm::vec3 init_pos = camera.get_position();
        glm::vec3 init_look = camera.get_forward();
        printf("[CAMERA-INITIAL-STATE] ============================================\n");
        printf("[CAMERA-INITIAL-STATE] pos=(%.6f, %.6f, %.6f)\n", init_pos.x, init_pos.y, init_pos.z);
        printf("[CAMERA-INITIAL-STATE] look_dir=(%.6f, %.6f, %.6f)\n", init_look.x, init_look.y, init_look.z);
        printf("[CAMERA-INITIAL-STATE] fov_deg=%.4f near=%.4f far=%.4f\n",
               camera.fov_degrees, camera.near_plane, camera.far_plane);
        printf("[CAMERA-INITIAL-STATE] (Move the camera with WASD/mouse. Press P to print current pose + delta.)\n");
        printf("[CAMERA-INITIAL-STATE] ============================================\n");
    }
    // === END INITIAL CAMERA STATE ===

    // === TEMPORARY DEBUG: compare authored camera look direction vs direction to model ===
    // Requested to diagnose why the camera is not pointing at the model on startup.
    // The two directions below should be (nearly) identical if the authored camera
    // is correctly pointed at the model's center. If they differ significantly,
    // the glTF camera node itself does not look exactly at the loaded geometry center,
    // or there is still a transform discrepancy on the model side.
    {
        glm::vec3 cam_pos   = camera.get_position();
        glm::vec3 cam_front = camera.get_forward();

        // Use the same "nice visual center" the AABB framing code uses.
        // This is better than raw node translation (1,1,1) because it accounts for
        // actual geometry bounds.
        auto [model_center, model_radius] =
            renderer.scene_manager.get_first_instance_framing_sphere();

        glm::vec3 to_model = (model_radius > 0.01f)
            ? glm::normalize(model_center - cam_pos)
            : glm::vec3(0.0f);

        float dot = glm::dot(cam_front, to_model);
        float angle_deg = (dot > -1.0f && dot < 1.0f)
            ? glm::degrees(std::acos(std::clamp(dot, -1.0f, 1.0f)))
            : 0.0f;

        printf("[CAMERA-POINTING-DEBUG] ============================================\n");
        printf("[CAMERA-POINTING-DEBUG] Camera pos:            (%.4f, %.4f, %.4f)\n",
               cam_pos.x, cam_pos.y, cam_pos.z);
        printf("[CAMERA-POINTING-DEBUG] Camera look dir:       (%.4f, %.4f, %.4f)\n",
               cam_front.x, cam_front.y, cam_front.z);
        printf("[CAMERA-POINTING-DEBUG] Model AABB center:     (%.4f, %.4f, %.4f)  radius=%.4f\n",
               model_center.x, model_center.y, model_center.z, model_radius);
        printf("[CAMERA-POINTING-DEBUG] Dir cam -> model ctr:  (%.4f, %.4f, %.4f)\n",
               to_model.x, to_model.y, to_model.z);
        printf("[CAMERA-POINTING-DEBUG] Dot product (1=perfect): %.6f\n", dot);
        printf("[CAMERA-POINTING-DEBUG] Angle between them:    %.2f degrees\n", angle_deg);
        printf("[CAMERA-POINTING-DEBUG] ============================================\n");
    }
    // === END TEMP DEBUG ===

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
                dst->lightCount = n;
                for (uint32_t i = 0; i < n; ++i) {
                    const auto &L = renderer.lights[i];
                    dst->lightDirectionsOrPositions[i] =
                        glm::vec4(L.positionOrDirection, 0.0f);
                    dst->lightColors[i] = glm::vec4(L.color, L.intensity);
                    dst->lightParams[i] =
                        glm::vec4(static_cast<float>(L.type), L.range,
                                  L.innerConeAngle, L.outerConeAngle);
                }
                // Leave higher slots (if any) as they were; lightCount gates them.
            }
        }
        printf("[LIGHTS] Using %u scene light(s) from glTF (world-transformed)\n", n);
        for (uint32_t i = 0; i < n; ++i) {
            const auto &L = renderer.lights[i];
            const char* typeName = (L.type == gfx::LightType::Directional) ? "Directional" :
                                   (L.type == gfx::LightType::Point) ? "Point" : "Spot";
            printf("[LIGHTS]   [%u] %s  pos/dir=(%.4f %.4f %.4f)  color=(%.3f %.3f %.3f)  intensity=%.3f  range=%.3f\n",
                   i, typeName,
                   L.positionOrDirection.x, L.positionOrDirection.y, L.positionOrDirection.z,
                   L.color.r, L.color.g, L.color.b, L.intensity, L.range);
        }
    } else {
        printf("[LIGHTS] No scene lights; using engine globalLight fallback\n");
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