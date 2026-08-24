#include "ecs/LocomotionAnimSystem.h"
#include "ecs/World.h"
#include "scene/Animation.h"
#include "scene/SceneManager.h"
#include "scene/TransformManager.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#include <tiny_gltf.h>

namespace ecs {
namespace {

bool name_ieq(const std::string& a, const char* b) {
    if (!b)
        return false;
    const size_t n = std::strlen(b);
    if (a.size() != n)
        return false;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

bool is_under(scene::TransformManager& xforms, uint32_t node, uint32_t ancestor) {
    if (node == scene::TransformManager::kInvalid ||
        ancestor == scene::TransformManager::kInvalid)
        return false;
    uint32_t w = node;
    for (int i = 0; i < 48; ++i) {
        const uint32_t p = xforms.get_parent(w);
        if (p == ancestor)
            return true;
        if (p == scene::TransformManager::kInvalid)
            return false;
        w = p;
    }
    return false;
}

uint32_t clip_or_none(scene::AnimationSystem& anims, const std::string& name) {
    const int idx = anims.find_clip(name.c_str());
    return (idx >= 0) ? static_cast<uint32_t>(idx) : ~0u;
}

uint32_t clip_for_state(const LocomotionAnim& loco, LocomotionAnim::State s) {
    switch (s) {
    case LocomotionAnim::State::Walk:
        return loco.walk_clip;
    case LocomotionAnim::State::Run:
        return loco.run_clip;
    case LocomotionAnim::State::Stand:
    default:
        return loco.idle_clip;
    }
}

const char* state_name(LocomotionAnim::State s) {
    switch (s) {
    case LocomotionAnim::State::Walk:
        return "walk";
    case LocomotionAnim::State::Run:
        return "run";
    case LocomotionAnim::State::Stand:
    default:
        return "idle";
    }
}

} // namespace

void bind_player_animation_masks(World& world, scene::SceneManager& scene,
                                 const tinygltf::Model& model) {
    const Entity player = world.active_player();
    if (player == kInvalidEntity)
        return;
    const TransformLink* link = world.transform_links.try_get(player);
    if (!link || link->transform_index == ~0u)
        return;

    auto& anims = scene.animations();
    auto& xforms = scene.transforms();
    anims.ignore_transform_trs(link->transform_index);

    const auto& n2x = scene.gltf_node_to_transform();
    uint32_t masked_root = scene::TransformManager::kInvalid;
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        if (i >= n2x.size())
            continue;
        const uint32_t xi = n2x[i];
        if (xi == scene::TransformManager::kInvalid ||
            xi == link->transform_index)
            continue;
        if (!is_under(xforms, xi, link->transform_index))
            continue;
        if (name_ieq(model.nodes[i].name, "root")) {
            anims.ignore_transform_translation(xi);
            masked_root = xi;
        }
    }

    LOG_INFO("[Anim] player root TRS masked xform=" << link->transform_index
             << " skeleton_root_T="
             << (masked_root == scene::TransformManager::kInvalid
                     ? -1
                     : static_cast<int>(masked_root)));
}

void locomotion_anim_bind_clips(World& world, scene::AnimationSystem& anims) {
    if (anims.clip_count() == 0)
        return;
    for (uint32_t i = 0; i < world.locomotion_anims.size(); ++i) {
        const Entity e = world.locomotion_anims.entities()[i];
        LocomotionAnim& loco = world.locomotion_anims.data()[i];
        loco.idle_clip = clip_or_none(anims, loco.idle_name);
        if (loco.idle_clip == ~0u)
            loco.idle_clip = clip_or_none(anims, "Idle");
        if (loco.idle_clip == ~0u)
            loco.idle_clip = clip_or_none(anims, "T-Pose");
        loco.walk_clip = clip_or_none(anims, loco.walk_name);
        loco.run_clip = clip_or_none(anims, loco.run_name);
        if (loco.walk_clip == ~0u)
            loco.walk_clip = loco.idle_clip;
        if (loco.run_clip == ~0u)
            loco.run_clip = loco.walk_clip;
        loco.clips_bound = true;
        loco.state = LocomotionAnim::State::Stand;

        const uint32_t idle = clip_for_state(loco, LocomotionAnim::State::Stand);
        if (idle != ~0u)
            anims.play_exclusive(idle, true, 1.0f);

        LOG_INFO("[ECS] locomotion clips entity=" << e << " idle="
                 << static_cast<int>(loco.idle_clip) << " walk="
                 << static_cast<int>(loco.walk_clip) << " run="
                 << static_cast<int>(loco.run_clip) << " walk_th="
                 << loco.walk_threshold << " run_th=" << loco.run_threshold);
    }
}

void locomotion_anim_system_update(World& world, scene::AnimationSystem& anims) {
    if (world.locomotion_anims.size() == 0 || anims.clip_count() == 0)
        return;

    for (uint32_t i = 0; i < world.locomotion_anims.size(); ++i) {
        const Entity e = world.locomotion_anims.entities()[i];
        LocomotionAnim& loco = world.locomotion_anims.data()[i];
        if (!loco.clips_bound)
            continue;

        float speed = 0.0f;
        if (const FpsMove* fps = world.fps_moves.try_get(e))
            speed = fps->horizontal_speed;

        const float walk_enter = loco.walk_threshold;
        const float walk_exit = loco.walk_threshold * 0.5f;
        const float run_enter = loco.run_threshold;
        const float run_exit = loco.run_threshold * 0.8f;

        LocomotionAnim::State next = loco.state;
        switch (loco.state) {
        case LocomotionAnim::State::Stand:
            if (speed >= run_enter)
                next = LocomotionAnim::State::Run;
            else if (speed >= walk_enter)
                next = LocomotionAnim::State::Walk;
            break;
        case LocomotionAnim::State::Walk:
            if (speed >= run_enter)
                next = LocomotionAnim::State::Run;
            else if (speed < walk_exit)
                next = LocomotionAnim::State::Stand;
            break;
        case LocomotionAnim::State::Run:
            if (speed < walk_exit)
                next = LocomotionAnim::State::Stand;
            else if (speed < run_exit)
                next = LocomotionAnim::State::Walk;
            break;
        }

        if (next == loco.state)
            continue;
        const uint32_t clip = clip_for_state(loco, next);
        if (clip == ~0u)
            continue;
        loco.state = next;
        anims.crossfade(clip, std::max(0.0f, loco.fade), true, 1.0f);
        LOG_INFO("[Loco] " << state_name(next) << " speed=" << speed << " clip='"
                 << anims.clip(clip).name << "'");
    }
}

} // namespace ecs
