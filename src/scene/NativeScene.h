#pragma once

// Native scene cache (.abn). Little-endian. Version 2 writes one zstd
// frame of the logical document after the stamp header. A hit skips
// tinygltf, image decode, meshoptimizer, and Jolt hull cooking. The
// stamp is the source file size + mtime, worldScale, optimizeMeshes,
// scenePhysics, and the Jolt binary version. GPU upload still happens
// at load.

#include "ecs/Components.h"
#include "ecs/Entity.h"
#include "gfx/Light.h"
#include "gfx/Material.h"
#include "gfx/MeshData.h"
#include "gfx/TextureManager.h"
#include "physics/PhysicsWorld.h"
#include "scene/Animation.h"
#include "scene/GameObject.h"
#include "scene/Morph.h"
#include "scene/RenderMesh.h"
#include "scene/Skin.h"
#include "scene/TransformManager.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace scene {

constexpr uint32_t kNativeSceneVersion = 2;
constexpr uint32_t kNativeFlagOptimizeMeshes = 1u << 0;
constexpr uint32_t kNativeFlagScenePhysics = 1u << 1;

struct NativeMesh {
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<gfx::MeshletDesc> meshlets;
    core::AABB local_aabb{};
    bool index16 = false;
    bool allow_meshlet_cull = true;
};

struct NativeXform {
    uint32_t parent = TransformManager::kInvalid;
    LocalTrs trs{};
};

struct NativeCamera {
    bool valid = false;
    float yfov = 0.0f;
    float aspect = 0.0f;
    float znear = 0.1f;
    float zfar = 100.0f;
    int gltf_index = -1;
    uint32_t transform_index = TransformManager::kInvalid;
};

struct NativeEcs {
    uint32_t entity_count = 0;
    std::vector<uint8_t> alive;
    std::vector<std::pair<ecs::Entity, ecs::TransformLink>> transform_links;
    std::vector<ecs::Entity> player_tags;
    std::vector<ecs::Entity> desktop_moves;
    std::vector<std::pair<ecs::Entity, ecs::FpsMove>> fps_moves;
    std::vector<std::pair<ecs::Entity, ecs::CameraRig>> camera_rigs;
    std::vector<std::pair<ecs::Entity, ecs::Health>> healths;
    std::vector<std::pair<ecs::Entity, ecs::Name>> names;
    std::vector<std::pair<ecs::Entity, std::string>> scripts;
    std::vector<std::pair<ecs::Entity, ecs::LocomotionAnim>> locomotion;
    std::vector<ecs::Entity> gltf_node_to_entity;
    std::vector<ecs::Entity> game_object_to_entity;
};

struct NativeSceneFile {
    std::vector<gfx::CachedImage> images;
    std::vector<gfx::Material> materials;
    std::vector<NativeMesh> meshes;
    std::vector<NativeXform> xforms;
    std::vector<uint32_t> node_to_xform;
    std::vector<std::string> node_names;
    std::vector<GameObject> game_objects;
    std::vector<RenderMesh> render_meshes;
    std::vector<Skin> skins;
    std::vector<AnimationClip> clips;
    std::vector<MorphInstance> morphs;
    std::vector<uint32_t> node_to_morph;
    std::vector<gfx::Light> lights;
    gfx::Light global_light{};
    NativeCamera camera{};
    NativeEcs ecs{};
    std::vector<physics::PhysicsWorld::CookedBody> bodies;
};

bool source_file_stamp(const std::filesystem::path& source, uint64_t& size,
                       uint64_t& mtime);

std::filesystem::path native_cache_path(const std::string& scene_name);

enum class NativeStamp { Missing, Mismatch, Match };

NativeStamp native_cache_stamp(const std::filesystem::path& file,
                               const std::string& scene_name, float world_scale,
                               uint32_t flags, uint64_t source_size,
                               uint64_t source_mtime);

bool write_native_scene(const std::filesystem::path& file,
                        const std::string& scene_name, float world_scale,
                        uint32_t flags, uint64_t source_size,
                        uint64_t source_mtime, const NativeSceneFile& scene);

bool read_native_scene(const std::filesystem::path& file,
                       const std::string& scene_name, float world_scale,
                       uint32_t flags, uint64_t source_size,
                       uint64_t source_mtime, NativeSceneFile& scene);

} // namespace scene
