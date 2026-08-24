#include "physics/PhysicsWorld.h"
#include "core/Log.h"
#include "scene/TransformManager.h"

// Jolt: always include Jolt.h first.
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <thread>
#include <vector>

JPH_SUPPRESS_WARNINGS

namespace physics {
namespace {

using namespace JPH;

void jolt_trace(const char* fmt, ...) {
    char buffer[1024];
    va_list list;
    va_start(list, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, list);
    va_end(list);
    LOG_INFO("[Jolt] " << buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool jolt_assert_failed(const char* expr, const char* msg, const char* file,
                        uint line) {
    LOG_ERROR("[Jolt assert] " << file << ":" << line << " (" << expr << ") "
                               << (msg ? msg : ""));
    return true;
}
#endif

namespace Layers {
constexpr ObjectLayer NON_MOVING = 0;
constexpr ObjectLayer MOVING = 1;
constexpr ObjectLayer NUM_LAYERS = 2;
} // namespace Layers

namespace BPLayers {
constexpr BroadPhaseLayer NON_MOVING(0);
constexpr BroadPhaseLayer MOVING(1);
constexpr uint NUM_LAYERS = 2;
} // namespace BPLayers

class ObjectLayerPairFilterImpl final : public ObjectLayerPairFilter {
  public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        switch (a) {
        case Layers::NON_MOVING:
            return b == Layers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            return false;
        }
    }
};

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
  public:
    BPLayerInterfaceImpl() {
        map_[Layers::NON_MOVING] = BPLayers::NON_MOVING;
        map_[Layers::MOVING] = BPLayers::MOVING;
    }

    uint GetNumBroadPhaseLayers() const override { return BPLayers::NUM_LAYERS; }

    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        JPH_ASSERT(layer < Layers::NUM_LAYERS);
        return map_[layer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override {
        switch ((BroadPhaseLayer::Type)layer) {
        case (BroadPhaseLayer::Type)BPLayers::NON_MOVING:
            return "NON_MOVING";
        case (BroadPhaseLayer::Type)BPLayers::MOVING:
            return "MOVING";
        default:
            return "INVALID";
        }
    }
#endif

  private:
    BroadPhaseLayer map_[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl final
    : public ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(ObjectLayer layer1, BroadPhaseLayer layer2) const override {
        switch (layer1) {
        case Layers::NON_MOVING:
            return layer2 == BPLayers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            return false;
        }
    }
};

EMotionType to_jolt_motion(MotionType m) {
    switch (m) {
    case MotionType::Static:
        return EMotionType::Static;
    case MotionType::Kinematic:
        return EMotionType::Kinematic;
    case MotionType::Dynamic:
    default:
        return EMotionType::Dynamic;
    }
}

ObjectLayer layer_for_motion(MotionType m) {
    return (m == MotionType::Static) ? Layers::NON_MOVING : Layers::MOVING;
}

// glm (w,x,y,z) → Jolt Quat (x,y,z,w)
Quat to_jolt_quat(const glm::quat& q) {
    return Quat(q.x, q.y, q.z, q.w);
}

// World matrices include worldScale. quat_cast on a scaled mat3 is not a
// rotation — yaw drifts a few degrees either way as the player turns.
glm::quat rotation_from_world(const glm::mat4& w) {
    glm::vec3 c0(w[0]), c1(w[1]), c2(w[2]);
    const float s0 = glm::length(c0);
    const float s1 = glm::length(c1);
    const float s2 = glm::length(c2);
    if (s0 > 1e-8f)
        c0 /= s0;
    if (s1 > 1e-8f)
        c1 /= s1;
    if (s2 > 1e-8f)
        c2 /= s2;
    if (glm::dot(c2, glm::cross(c0, c1)) < 0.0f)
        c2 = -c2;
    return glm::normalize(glm::quat_cast(glm::mat3(c0, c1, c2)));
}

glm::quat to_glm_quat(Quat q) {
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

// One-time process-wide Jolt type registration (safe to call once).
std::atomic<int> g_jolt_users{0};

} // namespace

struct PhysicsWorld::Impl {
    TempAllocatorImpl temp_allocator{10 * 1024 * 1024};
    JobSystemThreadPool job_system{
        cMaxPhysicsJobs, cMaxPhysicsBarriers,
        static_cast<int>(
            std::max(1u, std::thread::hardware_concurrency() > 0
                             ? std::thread::hardware_concurrency() - 1
                             : 1u))};
    BPLayerInterfaceImpl broad_phase_layers{};
    ObjectVsBroadPhaseLayerFilterImpl obj_vs_bp{};
    ObjectLayerPairFilterImpl obj_vs_obj{};
    PhysicsSystem system{};

    struct BodyRecord {
        BodyID id;
        uint32_t transform_index = ~0u;
        MotionType motion = MotionType::Static;
    };
    std::vector<BodyRecord> bodies{};

    float accumulator = 0.0f;
    static constexpr float kFixedDt = 1.0f / 60.0f;
};

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld() {
    shutdown();
}

bool PhysicsWorld::initialize() {
    if (initialized_)
        return true;

    if (g_jolt_users.fetch_add(1) == 0) {
        RegisterDefaultAllocator();
        Trace = jolt_trace;
        JPH_IF_ENABLE_ASSERTS(AssertFailed = jolt_assert_failed;)
        Factory::sInstance = new Factory();
        RegisterTypes();
    }

    impl_ = new Impl();

    constexpr uint cMaxBodies = 4096;
    constexpr uint cNumBodyMutexes = 0;
    constexpr uint cMaxBodyPairs = 4096;
    constexpr uint cMaxContactConstraints = 4096;

    impl_->system.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs,
                       cMaxContactConstraints, impl_->broad_phase_layers,
                       impl_->obj_vs_bp, impl_->obj_vs_obj);

    // Default gravity (Y-up, matching glTF / engine).
    impl_->system.SetGravity(Vec3(0.0f, -9.81f, 0.0f));

    initialized_ = true;
    LOG_INFO("[Physics] Jolt PhysicsSystem initialized (maxBodies="
             << cMaxBodies << ")");
    return true;
}

void PhysicsWorld::shutdown() {
    if (!initialized_ || !impl_)
        return;

    BodyInterface& bi = impl_->system.GetBodyInterface();
    for (auto& rec : impl_->bodies) {
        if (!rec.id.IsInvalid()) {
            bi.RemoveBody(rec.id);
            bi.DestroyBody(rec.id);
            rec.id = BodyID();
        }
    }
    impl_->bodies.clear();

    delete impl_;
    impl_ = nullptr;
    initialized_ = false;

    if (g_jolt_users.fetch_sub(1) == 1) {
        UnregisterTypes();
        delete Factory::sInstance;
        Factory::sInstance = nullptr;
    }
    LOG_INFO("[Physics] shutdown");
}

BodyHandle PhysicsWorld::add_body_with_shape(void* shape_ref,
                                             const BodyPoseDesc& pose,
                                             const char* label) {
    if (!impl_ || !shape_ref)
        return kInvalidBody;

    ShapeRefC shape = *static_cast<ShapeRefC*>(shape_ref);
    const EMotionType motion = to_jolt_motion(pose.motion);
    const ObjectLayer layer = layer_for_motion(pose.motion);
    const RVec3 pos(pose.position.x, pose.position.y, pose.position.z);

    BodyCreationSettings settings(shape, pos, to_jolt_quat(pose.rotation), motion,
                                  layer);
    settings.mRestitution = pose.restitution;
    settings.mFriction = pose.friction;
    // Continuous collision (linear cast) so small/fast dynamics don't tunnel
    // thin statics (floors, tabletops). Discrete is fine for statics/kinematics.
    if (pose.motion == MotionType::Dynamic)
        settings.mMotionQuality = EMotionQuality::LinearCast;
    if (pose.motion == MotionType::Dynamic && pose.mass > 0.0f) {
        settings.mOverrideMassProperties =
            EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = pose.mass;
    }

    BodyInterface& bi = impl_->system.GetBodyInterface();
    const EActivation act = (pose.motion == MotionType::Static)
                                ? EActivation::DontActivate
                                : EActivation::Activate;
    const BodyID id = bi.CreateAndAddBody(settings, act);
    if (id.IsInvalid()) {
        LOG_ERROR("[Physics] CreateAndAddBody failed (" << label << ")");
        return kInvalidBody;
    }

    Impl::BodyRecord rec{};
    rec.id = id;
    rec.transform_index = pose.transform_index;
    rec.motion = pose.motion;
    const BodyHandle handle = static_cast<BodyHandle>(impl_->bodies.size());
    impl_->bodies.push_back(rec);
    return handle;
}

BodyHandle PhysicsWorld::create_box(const BoxDesc& desc) {
    if (!initialized_ || !impl_)
        return kInvalidBody;

    // Jolt's default convex radius (~0.05 m) exceeds half-extents on small
    // boxes and fails with "Invalid convex radius". Scale radius with size.
    const float hx = std::max(desc.half_extents.x, 1e-4f);
    const float hy = std::max(desc.half_extents.y, 1e-4f);
    const float hz = std::max(desc.half_extents.z, 1e-4f);
    const Vec3 half(hx, hy, hz);
    const float convex_r =
        std::min(0.05f, 0.25f * std::min({hx, hy, hz}));
    BoxShapeSettings shape_settings(half, convex_r);
    shape_settings.SetEmbedded();
    ShapeSettings::ShapeResult shape_result = shape_settings.Create();
    if (shape_result.HasError()) {
        LOG_ERROR("[Physics] BoxShape create failed: "
                  << shape_result.GetError().c_str() << " half=(" << hx << ","
                  << hy << "," << hz << ") cr=" << convex_r);
        return kInvalidBody;
    }
    ShapeRefC shape = shape_result.Get();
    return add_body_with_shape(&shape, desc, "box");
}

BodyHandle PhysicsWorld::create_capsule(const CapsuleDesc& desc) {
    if (!initialized_ || !impl_)
        return kInvalidBody;

    const float radius = std::max(desc.radius, 1e-4f);
    // Jolt capsule half-height is cylindrical section only (excludes hemispheres).
    const float hh = std::max(desc.half_height, 1e-4f);
    CapsuleShapeSettings shape_settings(hh, radius);
    shape_settings.SetEmbedded();
    ShapeSettings::ShapeResult shape_result = shape_settings.Create();
    if (shape_result.HasError()) {
        LOG_ERROR("[Physics] CapsuleShape create failed: "
                  << shape_result.GetError().c_str() << " hh=" << hh
                  << " r=" << radius);
        return kInvalidBody;
    }
    ShapeRefC shape = shape_result.Get();
    return add_body_with_shape(&shape, desc, "capsule");
}

BodyHandle PhysicsWorld::create_convex_hull(const ConvexHullDesc& desc) {
    if (!initialized_ || !impl_)
        return kInvalidBody;
    if (desc.points.size() < 4) {
        LOG_ERROR("[Physics] ConvexHull needs >= 4 points (got "
                  << desc.points.size() << ")");
        return kInvalidBody;
    }

    glm::vec3 bmin(1e30f), bmax(-1e30f);
    for (const glm::vec3& p : desc.points) {
        bmin = glm::min(bmin, p);
        bmax = glm::max(bmax, p);
    }

    // Dense render meshes (100k+ verts) make Jolt's hull error check fail.
    // Do NOT inject AABB corners — those are not on the mesh, so the hull
    // becomes a box. Keep silhouette support points instead.
    constexpr size_t kDirectMax = 2048;
    constexpr int kSupportDirs = 48;
    std::vector<glm::vec3> thinned;
    const std::vector<glm::vec3>* src = &desc.points;
    if (desc.points.size() > kDirectMax) {
        thinned.resize(static_cast<size_t>(kSupportDirs));
        for (int i = 0; i < kSupportDirs; ++i) {
            const float z =
                1.0f - 2.0f * (static_cast<float>(i) + 0.5f) /
                           static_cast<float>(kSupportDirs);
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float th =
                2.3999632f * static_cast<float>(i); // golden angle
            const glm::vec3 d(r * std::cos(th), z, r * std::sin(th));
            float best = -1.0e30f;
            glm::vec3 bp = desc.points[0];
            for (const glm::vec3& p : desc.points) {
                const float s = glm::dot(p, d);
                if (s > best) {
                    best = s;
                    bp = p;
                }
            }
            thinned[static_cast<size_t>(i)] = bp;
        }
        src = &thinned;
        LOG_INFO("[Physics] ConvexHull support-sampled " << desc.points.size()
                 << " -> " << thinned.size() << " points");
    }

    Array<Vec3> pts;
    pts.reserve(src->size());
    for (const glm::vec3& p : *src)
        pts.push_back(Vec3(p.x, p.y, p.z));
    const glm::vec3 ext = bmax - bmin;
    const float convex_r =
        std::min(0.05f, 0.05f * std::max({ext.x, ext.y, ext.z, 1e-3f}));

    ConvexHullShapeSettings shape_settings(pts, convex_r);
    shape_settings.SetEmbedded();
    ShapeSettings::ShapeResult shape_result = shape_settings.Create();
    if (shape_result.HasError()) {
        LOG_ERROR("[Physics] ConvexHull create failed: "
                  << shape_result.GetError().c_str()
                  << " points=" << desc.points.size());
        BoxDesc box{};
        static_cast<BodyPoseDesc&>(box) = desc;
        const glm::vec3 local_half = glm::max(
            glm::max(glm::abs(bmin), glm::abs(bmax)), glm::vec3(1e-3f));
        box.half_extents = local_half;
        LOG_INFO("[Physics] ConvexHull fallback to origin-centered box half=("
                 << local_half.x << "," << local_half.y << "," << local_half.z
                 << ")");
        return create_box(box);
    }
    ShapeRefC shape = shape_result.Get();
    return add_body_with_shape(&shape, desc, "convex");
}

BodyHandle PhysicsWorld::create_floor(float half_extent_xz, float half_height,
                                      float y_center) {
    BoxDesc desc{};
    desc.half_extents = glm::vec3(half_extent_xz, half_height, half_extent_xz);
    desc.position = glm::vec3(0.0f, y_center, 0.0f);
    desc.motion = MotionType::Static;
    desc.restitution = 0.1f;
    desc.friction = 0.8f;
    return create_box(desc);
}

void PhysicsWorld::set_transform_link(BodyHandle body, uint32_t transform_index) {
    if (!impl_ || body >= impl_->bodies.size())
        return;
    impl_->bodies[body].transform_index = transform_index;
}

uint32_t PhysicsWorld::get_transform_link(BodyHandle body) const {
    if (!impl_ || body >= impl_->bodies.size())
        return ~0u;
    return impl_->bodies[body].transform_index;
}

void PhysicsWorld::step(float delta_time) {
    if (!initialized_ || !impl_)
        return;
    if (delta_time <= 0.0f)
        return;

    // Cap catch-up so a hitch doesn't spiral.
    constexpr float kMaxFrame = 0.25f;
    if (delta_time > kMaxFrame)
        delta_time = kMaxFrame;

    impl_->accumulator += delta_time;
    // At most 4 fixed steps per call.
    int steps = 0;
    while (impl_->accumulator >= Impl::kFixedDt && steps < 4) {
        const int collision_steps = 1;
        impl_->system.Update(Impl::kFixedDt, collision_steps, &impl_->temp_allocator,
                             &impl_->job_system);
        impl_->accumulator -= Impl::kFixedDt;
        ++steps;
    }
    // Drop excess if we hit the cap (avoid spiral of death).
    if (steps >= 4 && impl_->accumulator > Impl::kFixedDt * 2.0f)
        impl_->accumulator = 0.0f;
}

void PhysicsWorld::sync_from_transforms(const scene::TransformManager& transforms) {
    if (!initialized_ || !impl_)
        return;

    BodyInterface& bi = impl_->system.GetBodyInterface();
    for (const auto& rec : impl_->bodies) {
        if (rec.motion != MotionType::Kinematic)
            continue;
        if (rec.transform_index == ~0u || !transforms.is_alive(rec.transform_index))
            continue;

        // Hulls are posed at the node origin (T+R); use node world directly.
        const glm::mat4& w = transforms.get_world_matrix(rec.transform_index);
        const glm::vec3 pos(w[3]);
        const glm::quat rot = rotation_from_world(w);

        bi.SetPositionAndRotation(rec.id, RVec3(pos.x, pos.y, pos.z),
                                  to_jolt_quat(rot), EActivation::Activate);

        // Kinematic movers do not wake sleeping dynamics by themselves, so a
        // settled object would ignore a later shove. Activate anything in a
        // padded AABB around the hull.
        AABox box;
        {
            BodyLockRead lock(impl_->system.GetBodyLockInterface(), rec.id);
            if (!lock.Succeeded())
                continue;
            box = lock.GetBody().GetWorldSpaceBounds();
        }
        box.ExpandBy(Vec3::sReplicate(0.2f));
        SpecifiedBroadPhaseLayerFilter bp(BPLayers::MOVING);
        SpecifiedObjectLayerFilter obj(Layers::MOVING);
        bi.ActivateBodiesInAABox(box, bp, obj);
    }
}

void PhysicsWorld::sync_to_transforms(scene::TransformManager& transforms) {
    if (!initialized_ || !impl_)
        return;

    BodyInterface& bi = impl_->system.GetBodyInterface();
    for (const auto& rec : impl_->bodies) {
        if (rec.transform_index == ~0u)
            continue;
        if (!transforms.is_alive(rec.transform_index))
            continue;
        // Only dynamics: statics are immovable; kinematics are driven by gameplay.
        if (rec.motion != MotionType::Dynamic)
            continue;

        const RVec3 p = bi.GetPosition(rec.id);
        const Quat r = bi.GetRotation(rec.id);
        const glm::vec3 pos(static_cast<float>(p.GetX()),
                            static_cast<float>(p.GetY()),
                            static_cast<float>(p.GetZ()));
        const glm::quat rot = to_glm_quat(r);

        scene::LocalTrs trs = transforms.get_local_trs(rec.transform_index);
        // Body center may not equal node origin if AABB was offset — store as
        // node translation for MVP (we create boxes with center at node origin
        // expanded to cover mesh; see spawn_scene_physics).
        trs.translation = pos;
        trs.rotation = rot;
        transforms.set_local_trs(rec.transform_index, trs);
    }
}

void PhysicsWorld::set_body_pose(BodyHandle body, const glm::vec3& position,
                                 const glm::quat& rotation) {
    if (!impl_ || body >= impl_->bodies.size())
        return;
    BodyInterface& bi = impl_->system.GetBodyInterface();
    bi.SetPositionAndRotation(impl_->bodies[body].id,
                              RVec3(position.x, position.y, position.z),
                              to_jolt_quat(rotation), EActivation::Activate);
}

bool PhysicsWorld::raycast_static(const glm::vec3& origin,
                                  const glm::vec3& direction, float max_distance,
                                  glm::vec3& out_hit) const {
    if (!initialized_ || !impl_ || max_distance <= 1e-8f)
        return false;
    glm::vec3 dir = direction;
    const float len = glm::length(dir);
    if (len < 1e-8f)
        return false;
    dir /= len;

    class StaticOnlyFilter final : public ObjectLayerFilter {
      public:
        bool ShouldCollide(ObjectLayer layer) const override {
            return layer == Layers::NON_MOVING;
        }
    };

    const RRayCast ray{
        RVec3(origin.x, origin.y, origin.z),
        Vec3(dir.x, dir.y, dir.z) * max_distance,
    };
    RayCastResult hit;
    StaticOnlyFilter filt{};
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, filt))
        return false;
    const float t = hit.mFraction * max_distance;
    out_hit = origin + dir * t;
    return true;
}

bool PhysicsWorld::get_pose(BodyHandle body, glm::vec3& out_pos,
                            glm::quat& out_rot) {
    if (!impl_ || body >= impl_->bodies.size())
        return false;
    BodyInterface& bi = impl_->system.GetBodyInterface();
    const BodyID id = impl_->bodies[body].id;
    const RVec3 p = bi.GetPosition(id);
    const Quat r = bi.GetRotation(id);
    out_pos = glm::vec3(static_cast<float>(p.GetX()), static_cast<float>(p.GetY()),
                        static_cast<float>(p.GetZ()));
    out_rot = to_glm_quat(r);
    return true;
}

bool PhysicsWorld::destroy_body(BodyHandle body) {
    if (!impl_ || body >= impl_->bodies.size())
        return false;
    Impl::BodyRecord& rec = impl_->bodies[body];
    if (rec.id.IsInvalid())
        return false;

    BodyInterface& bi = impl_->system.GetBodyInterface();
    bi.RemoveBody(rec.id);
    bi.DestroyBody(rec.id);
    rec.id = BodyID();
    rec.transform_index = ~0u;
    rec.motion = MotionType::Static;
    return true;
}

bool PhysicsWorld::is_body_alive(BodyHandle body) const {
    if (!impl_ || body >= impl_->bodies.size())
        return false;
    return !impl_->bodies[body].id.IsInvalid();
}

MotionType PhysicsWorld::get_motion_type(BodyHandle body) const {
    if (!impl_ || body >= impl_->bodies.size())
        return MotionType::Static;
    return impl_->bodies[body].motion;
}

uint32_t PhysicsWorld::body_count() const {
    if (!impl_)
        return 0u;
    uint32_t n = 0;
    for (const auto& rec : impl_->bodies) {
        if (!rec.id.IsInvalid())
            ++n;
    }
    return n;
}

uint32_t PhysicsWorld::body_slot_count() const {
    return impl_ ? static_cast<uint32_t>(impl_->bodies.size()) : 0u;
}

void PhysicsWorld::collect_debug_lines(std::vector<DebugVertex>& out,
                                       const glm::vec3& camera_pos) const {
    out.clear();
    if (!debug_draw_enabled_ || !initialized_ || !impl_)
        return;

#ifdef JPH_DEBUG_RENDERER
    class LineCollector final : public DebugRendererSimple {
      public:
        std::vector<DebugVertex>* target = nullptr;

        void DrawLine(RVec3Arg from, RVec3Arg to, ColorArg color) override {
            if (!target)
                return;
            const uint32_t rgba = color.GetUInt32();
            DebugVertex a{};
            a.position = glm::vec3(static_cast<float>(from.GetX()),
                                   static_cast<float>(from.GetY()),
                                   static_cast<float>(from.GetZ()));
            a.color = rgba;
            DebugVertex b = a;
            b.position = glm::vec3(static_cast<float>(to.GetX()),
                                   static_cast<float>(to.GetY()),
                                   static_cast<float>(to.GetZ()));
            target->push_back(a);
            target->push_back(b);
        }

        void DrawText3D(RVec3Arg, const string_view&, ColorArg,
                        float) override {}
    };

    LineCollector collector;
    collector.target = &out;
    collector.SetCameraPos(RVec3(camera_pos.x, camera_pos.y, camera_pos.z));

    BodyManager::DrawSettings settings;
    settings.mDrawShape = true;
    settings.mDrawShapeWireframe = true;
    settings.mDrawShapeColor = BodyManager::EShapeColor::MotionTypeColor;
    settings.mDrawBoundingBox = false;
    settings.mDrawVelocity = false;

    impl_->system.DrawBodies(settings, &collector);
    (void)camera_pos;
#else
    (void)camera_pos;
    static bool once = false;
    if (!once) {
        once = true;
        LOG_INFO("[Physics] debug draw unavailable (Jolt built without "
                 "JPH_DEBUG_RENDERER)");
    }
#endif
}

} // namespace physics
