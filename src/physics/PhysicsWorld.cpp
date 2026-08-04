#include "physics/PhysicsWorld.h"
#include "core/Log.h"
#include "scene/TransformManager.h"

// Jolt: always include Jolt.h first.
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <thread>

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

BodyHandle PhysicsWorld::create_box(const BoxDesc& desc) {
    if (!initialized_ || !impl_)
        return kInvalidBody;

    const Vec3 half(desc.half_extents.x, desc.half_extents.y, desc.half_extents.z);
    BoxShapeSettings shape_settings(half);
    shape_settings.SetEmbedded();
    ShapeSettings::ShapeResult shape_result = shape_settings.Create();
    if (shape_result.HasError()) {
        LOG_ERROR("[Physics] BoxShape create failed: "
                  << shape_result.GetError().c_str());
        return kInvalidBody;
    }
    ShapeRefC shape = shape_result.Get();

    const EMotionType motion = to_jolt_motion(desc.motion);
    const ObjectLayer layer = layer_for_motion(desc.motion);
    const RVec3 pos(desc.position.x, desc.position.y, desc.position.z);

    BodyCreationSettings settings(shape, pos, to_jolt_quat(desc.rotation), motion,
                                  layer);
    settings.mRestitution = desc.restitution;
    settings.mFriction = desc.friction;
    if (desc.motion == MotionType::Dynamic && desc.mass > 0.0f) {
        settings.mOverrideMassProperties =
            EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = desc.mass;
    }

    BodyInterface& bi = impl_->system.GetBodyInterface();
    const EActivation act = (desc.motion == MotionType::Static)
                                ? EActivation::DontActivate
                                : EActivation::Activate;
    const BodyID id = bi.CreateAndAddBody(settings, act);
    if (id.IsInvalid()) {
        LOG_ERROR("[Physics] CreateAndAddBody failed (out of bodies?)");
        return kInvalidBody;
    }

    Impl::BodyRecord rec{};
    rec.id = id;
    rec.transform_index = desc.transform_index;
    rec.motion = desc.motion;
    const BodyHandle handle = static_cast<BodyHandle>(impl_->bodies.size());
    impl_->bodies.push_back(rec);
    return handle;
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

void PhysicsWorld::sync_to_transforms(scene::TransformManager& transforms) {
    if (!initialized_ || !impl_)
        return;

    BodyInterface& bi = impl_->system.GetBodyInterface();
    for (const auto& rec : impl_->bodies) {
        if (rec.transform_index == ~0u)
            continue;
        if (!transforms.is_alive(rec.transform_index))
            continue;
        // Statics still write once so visuals match if linked; dynamics every frame.
        if (rec.motion == MotionType::Static)
            continue;

        const RVec3 p = bi.GetPosition(rec.id);
        const Quat r = bi.GetRotation(rec.id);
        const glm::vec3 pos(static_cast<float>(p.GetX()),
                            static_cast<float>(p.GetY()),
                            static_cast<float>(p.GetZ()));
        const glm::quat rot = to_glm_quat(r);

        scene::LocalTrs trs = transforms.get_local_trs(rec.transform_index);
        trs.translation = pos;
        trs.rotation = rot;
        // Keep authored scale (collider half-extents separate from mesh scale).
        transforms.set_local_trs(rec.transform_index, trs);
    }
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

uint32_t PhysicsWorld::body_count() const {
    return impl_ ? static_cast<uint32_t>(impl_->bodies.size()) : 0u;
}

} // namespace physics
