#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace scene {
class TransformManager;
}

namespace physics {

using BodyHandle = uint32_t;
constexpr BodyHandle kInvalidBody = ~0u;

enum class MotionType : uint8_t {
    Static = 0,
    Dynamic = 1,
    Kinematic = 2,
};

// Axis-aligned box collider (half-extents in local space, before rotation).
struct BoxDesc {
    glm::vec3 half_extents{0.5f};
    glm::vec3 position{0.0f};
    // glm quat is (w, x, y, z)
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    MotionType motion = MotionType::Dynamic;
    float restitution = 0.2f;
    float friction = 0.5f;
    // Dynamic only. 0 = auto mass from density 1000.
    float mass = 0.0f;
    // Optional link written by create_box if set (else use set_transform_link).
    uint32_t transform_index = ~0u;
};

// Thin Jolt wrapper: object/broadphase layers, step, body create, transform sync.
// Jolt headers stay in the .cpp (pimpl).
class PhysicsWorld {
  public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    bool initialize();
    void shutdown();
    [[nodiscard]] bool is_initialized() const { return initialized_; }

    // Create a box body. Returns kInvalidBody on failure.
    BodyHandle create_box(const BoxDesc& desc);

    // Convenience: large static floor (center at y_center, thin in Y).
    BodyHandle create_floor(float half_extent_xz = 50.0f,
                            float half_height = 0.5f,
                            float y_center = -0.5f);

    void set_transform_link(BodyHandle body, uint32_t transform_index);
    [[nodiscard]] uint32_t get_transform_link(BodyHandle body) const;

    // Fixed-step friendly: accumulates remainder, steps at 1/60 when possible.
    // collision_steps >= 1 (use >1 if delta_time >> 1/60).
    void step(float delta_time);

    // For each linked body: write world position + rotation into TransformManager
    // local TRS (translation/rotation). Preserves existing local scale.
    // Call before Engine::sync_scene_transforms / propagate.
    void sync_to_transforms(scene::TransformManager& transforms);

    bool get_pose(BodyHandle body, glm::vec3& out_pos, glm::quat& out_rot);

    [[nodiscard]] uint32_t body_count() const;

  private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool initialized_ = false;
};

} // namespace physics
