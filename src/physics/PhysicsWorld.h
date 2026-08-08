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

// Shared body pose / material fields.
struct BodyPoseDesc {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // w,x,y,z
    MotionType motion = MotionType::Dynamic;
    float restitution = 0.2f;
    float friction = 0.5f;
    // Dynamic only. 0 = auto mass from density / engine.
    float mass = 0.0f;
    uint32_t transform_index = ~0u;
};

// Axis-aligned box collider (half-extents in local space, before rotation).
struct BoxDesc : BodyPoseDesc {
    glm::vec3 half_extents{0.5f};
};

// Capsule along local Y (Jolt: cylinder height + hemispherical caps).
struct CapsuleDesc : BodyPoseDesc {
    float half_height = 0.5f; // half of cylindrical section
    float radius = 0.25f;
};

// Convex hull from local-space points (already scaled into body space).
struct ConvexHullDesc : BodyPoseDesc {
    std::vector<glm::vec3> points;
};

// LINE_LIST vertex for physics / debug overlays (16 bytes).
struct DebugVertex {
    glm::vec3 position{0.0f};
    uint32_t color = 0xffffffffu; // RGBA8
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
    BodyHandle create_capsule(const CapsuleDesc& desc);
    BodyHandle create_convex_hull(const ConvexHullDesc& desc);

    // Convenience: large static floor (center at y_center, thin in Y).
    BodyHandle create_floor(float half_extent_xz = 50.0f,
                            float half_height = 0.5f,
                            float y_center = -0.5f);

    void set_transform_link(BodyHandle body, uint32_t transform_index);
    [[nodiscard]] uint32_t get_transform_link(BodyHandle body) const;

    // Fixed-step friendly: accumulates remainder, steps at 1/60 when possible.
    // collision_steps >= 1 (use >1 if delta_time >> 1/60).
    void step(float delta_time);

    // Kinematic (and optional dynamic teleport): TransformManager → Jolt pose.
    // Call *before* step() so player capsules / movers drive the sim.
    void sync_from_transforms(const scene::TransformManager& transforms);

    // Dynamic only: Jolt pose → TransformManager local T+R (keeps scale).
    // Call after step(). Static/kinematic are not written (authoring / mover owns them).
    void sync_to_transforms(scene::TransformManager& transforms);

    // Immediate pose write (kinematic movers, teleports).
    void set_body_pose(BodyHandle body, const glm::vec3& position,
                       const glm::quat& rotation);

    bool get_pose(BodyHandle body, glm::vec3& out_pos, glm::quat& out_rot);

    [[nodiscard]] uint32_t body_count() const;

    // Wireframe colliders via Jolt DebugRenderer (Debug/Release Jolt builds).
    void set_debug_draw_enabled(bool enabled) { debug_draw_enabled_ = enabled; }
    [[nodiscard]] bool is_debug_draw_enabled() const {
        return debug_draw_enabled_;
    }
    // Appends LINE_LIST verts (pairs). camera_pos used for Jolt LOD.
    void collect_debug_lines(std::vector<DebugVertex>& out,
                             const glm::vec3& camera_pos) const;

  private:
    struct Impl;
    Impl* impl_ = nullptr;
    bool initialized_ = false;
    bool debug_draw_enabled_ = false;

    BodyHandle add_body_with_shape(void* shape_ref, const BodyPoseDesc& pose,
                                   const char* label);
};

} // namespace physics
