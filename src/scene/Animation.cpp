#include "scene/Animation.h"
#include "scene/Morph.h"
#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <glm/gtc/quaternion.hpp>
#include <tiny_gltf.h>

namespace scene {
namespace {

void read_accessor_floats(const tinygltf::Model& model, int accessor_index,
                          std::vector<float>& out) {
    out.clear();
    if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size()))
        return;
    const auto& acc = model.accessors[static_cast<size_t>(accessor_index)];
    if (acc.bufferView < 0 || acc.bufferView >= static_cast<int>(model.bufferViews.size()))
        return;
    const auto& bv = model.bufferViews[static_cast<size_t>(acc.bufferView)];
    if (bv.buffer < 0 || bv.buffer >= static_cast<int>(model.buffers.size()))
        return;
    const auto& buf = model.buffers[static_cast<size_t>(bv.buffer)];

    const int comps = tinygltf::GetNumComponentsInType(acc.type);
    const int comp_size = tinygltf::GetComponentSizeInBytes(acc.componentType);
    int stride = acc.ByteStride(bv);
    if (stride <= 0)
        stride = comps * comp_size;

    const uint8_t* base =
        buf.data.data() + bv.byteOffset + acc.byteOffset;
    out.resize(static_cast<size_t>(acc.count) * static_cast<size_t>(comps));

    for (size_t i = 0; i < acc.count; ++i) {
        const uint8_t* ptr = base + i * static_cast<size_t>(stride);
        for (int c = 0; c < comps; ++c) {
            float v = 0.0f;
            switch (acc.componentType) {
            case TINYGLTF_COMPONENT_TYPE_FLOAT:
                v = reinterpret_cast<const float*>(ptr)[c];
                break;
            case TINYGLTF_COMPONENT_TYPE_DOUBLE:
                v = static_cast<float>(reinterpret_cast<const double*>(ptr)[c]);
                break;
            default:
                // Animation outputs are almost always float; skip exotic types.
                v = 0.0f;
                break;
            }
            out[i * static_cast<size_t>(comps) + static_cast<size_t>(c)] = v;
        }
    }
}

uint32_t components_for_path(AnimationPath path) {
    if (path == AnimationPath::Rotation)
        return 4u;
    if (path == AnimationPath::Weights)
        return 0u; // variable — use sampler inference
    return 3u;
}

AnimationInterp parse_interp(const std::string& s) {
    if (s == "STEP")
        return AnimationInterp::Step;
    if (s == "CUBICSPLINE")
        return AnimationInterp::CubicSpline;
    return AnimationInterp::Linear;
}

// Find keyframe interval for time in [times.front, times.back].
void find_keys(const std::vector<float>& times, float t, size_t& i0, size_t& i1,
               float& u) {
    if (times.empty()) {
        i0 = i1 = 0;
        u = 0.0f;
        return;
    }
    if (t <= times.front()) {
        i0 = i1 = 0;
        u = 0.0f;
        return;
    }
    if (t >= times.back()) {
        i0 = i1 = times.size() - 1;
        u = 0.0f;
        return;
    }
    // Binary search for first time > t
    size_t lo = 0, hi = times.size() - 1;
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        if (times[mid] <= t)
            lo = mid;
        else
            hi = mid;
    }
    i0 = lo;
    i1 = hi;
    const float dt = times[i1] - times[i0];
    u = (dt > 1e-8f) ? (t - times[i0]) / dt : 0.0f;
}

glm::quat nlerp(glm::quat a, glm::quat b, float t) {
    a = glm::normalize(a);
    b = glm::normalize(b);
    if (glm::dot(a, b) < 0.0f)
        b = -b;
    return glm::normalize(a * (1.0f - t) + b * t);
}

} // namespace

void AnimationSystem::clear() {
    clips_.clear();
    players_.clear();
    masks_.clear();
}

uint32_t AnimationSystem::load_from_gltf(
    const tinygltf::Model& model,
    const std::vector<uint32_t>& gltf_node_to_transform,
    const MorphSystem* morphs) {
    clear();
    clips_.reserve(model.animations.size());

    for (size_t ai = 0; ai < model.animations.size(); ++ai) {
        const auto& ta = model.animations[ai];
        AnimationClip clip{};
        clip.name = ta.name.empty() ? ("anim_" + std::to_string(ai)) : ta.name;
        clip.samplers.resize(ta.samplers.size());

        for (size_t si = 0; si < ta.samplers.size(); ++si) {
            const auto& ts = ta.samplers[si];
            AnimationSamplerData& s = clip.samplers[si];
            s.interp = parse_interp(ts.interpolation);
            read_accessor_floats(model, ts.input, s.times);
            read_accessor_floats(model, ts.output, s.values);
            // Infer component count from first key if possible.
            if (!s.times.empty() && !s.values.empty()) {
                const size_t n = s.times.size();
                if (s.interp == AnimationInterp::CubicSpline && n > 0) {
                    // output layout: n keys * 3 tangents blocks * comps
                    s.component_count =
                        static_cast<uint32_t>(s.values.size() / (n * 3));
                } else {
                    s.component_count =
                        static_cast<uint32_t>(s.values.size() / n);
                }
                if (s.component_count == 0)
                    s.component_count = 3;
            }
            if (!s.times.empty())
                clip.duration = std::max(clip.duration, s.times.back());
        }

        for (const auto& tc : ta.channels) {
            if (tc.target_node < 0)
                continue;
            const size_t node = static_cast<size_t>(tc.target_node);

            AnimationPath path = AnimationPath::Translation;
            if (tc.target_path == "rotation")
                path = AnimationPath::Rotation;
            else if (tc.target_path == "scale")
                path = AnimationPath::Scale;
            else if (tc.target_path == "translation")
                path = AnimationPath::Translation;
            else if (tc.target_path == "weights")
                path = AnimationPath::Weights;
            else
                continue;

            if (tc.sampler < 0 ||
                static_cast<size_t>(tc.sampler) >= clip.samplers.size())
                continue;

            AnimationChannel ch{};
            ch.sampler_index = static_cast<uint32_t>(tc.sampler);
            ch.path = path;

            if (path == AnimationPath::Weights) {
                if (!morphs)
                    continue;
                const uint32_t morph = morphs->morph_for_node(
                    static_cast<uint32_t>(node));
                if (morph == kInvalidMorph)
                    continue;
                ch.morph_index = morph;
                ch.transform_index = TransformManager::kInvalid;
            } else {
                if (node >= gltf_node_to_transform.size())
                    continue;
                const uint32_t xform = gltf_node_to_transform[node];
                if (xform == TransformManager::kInvalid)
                    continue;
                ch.transform_index = xform;
            }

            clip.channels.push_back(ch);
        }

        if (clip.channels.empty()) {
            LOG_INFO("[Anim] Skipping empty clip '" << clip.name << "'");
            continue;
        }
        clips_.push_back(std::move(clip));
    }

    LOG_INFO("[Anim] Loaded " << clips_.size() << " clip(s) from glTF ("
             << model.animations.size() << " in file)");
    return static_cast<uint32_t>(clips_.size());
}

void AnimationSystem::sample_channel(const AnimationSamplerData& samp, float time,
                                     float* out_components) {
    const uint32_t comps = std::max(1u, samp.component_count);
    for (uint32_t c = 0; c < comps; ++c)
        out_components[c] = 0.0f;

    if (samp.times.empty() || samp.values.empty())
        return;

    size_t i0 = 0, i1 = 0;
    float u = 0.0f;
    find_keys(samp.times, time, i0, i1, u);

    if (samp.interp == AnimationInterp::Step || i0 == i1) {
        const size_t base = i0 * comps;
        for (uint32_t c = 0; c < comps && base + c < samp.values.size(); ++c)
            out_components[c] = samp.values[base + c];
        return;
    }

    if (samp.interp == AnimationInterp::CubicSpline) {
        // values layout per key: in_tangent[comps], value[comps], out_tangent[comps]
        const size_t stride = comps * 3;
        const size_t b0 = i0 * stride + comps; // value offset
        const size_t b1 = i1 * stride + comps;
        // Hermite with out-tangent of i0 and in-tangent of i1
        const size_t out_t0 = i0 * stride + 2 * comps;
        const size_t in_t1 = i1 * stride;
        const float t = u;
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float h00 = 2.f * t3 - 3.f * t2 + 1.f;
        const float h10 = t3 - 2.f * t2 + t;
        const float h01 = -2.f * t3 + 3.f * t2;
        const float h11 = t3 - t2;
        const float dt = samp.times[i1] - samp.times[i0];
        for (uint32_t c = 0; c < comps; ++c) {
            const float p0 = samp.values[b0 + c];
            const float p1 = samp.values[b1 + c];
            const float m0 = samp.values[out_t0 + c] * dt;
            const float m1 = samp.values[in_t1 + c] * dt;
            out_components[c] = h00 * p0 + h10 * m0 + h01 * p1 + h11 * m1;
        }
        return;
    }

    // LINEAR
    const size_t b0 = i0 * comps;
    const size_t b1 = i1 * comps;
    if (comps == 4) {
        // glTF stores quaternions as [x,y,z,w]
        glm::quat q0(samp.values[b0 + 3], samp.values[b0], samp.values[b0 + 1],
                     samp.values[b0 + 2]);
        glm::quat q1(samp.values[b1 + 3], samp.values[b1], samp.values[b1 + 1],
                     samp.values[b1 + 2]);
        glm::quat q = nlerp(q0, q1, u);
        out_components[0] = q.x;
        out_components[1] = q.y;
        out_components[2] = q.z;
        out_components[3] = q.w;
    } else {
        for (uint32_t c = 0; c < comps; ++c) {
            const float a = samp.values[b0 + c];
            const float b = samp.values[b1 + c];
            out_components[c] = a + (b - a) * u;
        }
    }
}

void AnimationSystem::apply_channel(TransformManager& transforms,
                                    MorphSystem* morphs,
                                    const AnimationChannel& ch,
                                    const float* values) {
    switch (ch.path) {
    case AnimationPath::Translation:
        if (transforms.is_alive(ch.transform_index))
            transforms.set_local_translation(
                ch.transform_index,
                glm::vec3(values[0], values[1], values[2]));
        break;
    case AnimationPath::Rotation: {
        if (!transforms.is_alive(ch.transform_index))
            break;
        // Sampled as x,y,z,w
        glm::quat q(values[3], values[0], values[1], values[2]);
        transforms.set_local_rotation(ch.transform_index, glm::normalize(q));
        break;
    }
    case AnimationPath::Scale:
        if (transforms.is_alive(ch.transform_index))
            transforms.set_local_scale(
                ch.transform_index,
                glm::vec3(values[0], values[1], values[2]));
        break;
    case AnimationPath::Weights:
        if (morphs && ch.morph_index != kInvalidMorph) {
            // component count is sampler-inferred; MorphSystem clamps.
            // Sample writes into tmp; caller uses full sampler.component_count.
            morphs->set_weights(ch.morph_index, values, /*count=*/64);
        }
        break;
    }
}

bool AnimationSystem::channel_masked(const AnimationChannel& ch) const {
    if (ch.path == AnimationPath::Weights)
        return false;
    const uint8_t bit = (ch.path == AnimationPath::Translation) ? 1u
                       : (ch.path == AnimationPath::Rotation)    ? 2u
                                                                 : 4u;
    for (const ChannelMask& m : masks_) {
        if (m.transform_index == ch.transform_index && (m.path_bits & bit))
            return true;
    }
    return false;
}

void AnimationSystem::finish_completed_fades() {
    bool incoming_done = false;
    for (const auto& p : players_) {
        if (p.playing && !p.fade_out && p.fade_duration > 0.0f &&
            p.fade_age >= p.fade_duration)
            incoming_done = true;
    }
    if (!incoming_done)
        return;

    std::vector<AnimationPlayer> kept;
    kept.reserve(players_.size());
    for (auto& p : players_) {
        if (!p.playing || p.fade_out)
            continue;
        p.weight = 1.0f;
        p.fade_duration = 0.0f;
        p.fade_age = 0.0f;
        kept.push_back(p);
    }
    players_.swap(kept);
}

void AnimationSystem::update(float delta_time, TransformManager& transforms,
                             MorphSystem* morphs) {
    const float dt = std::max(delta_time, 0.0f);
    int playing_n = 0;
    for (auto& p : players_) {
        if (!p.playing || p.clip_index >= clips_.size())
            continue;
        ++playing_n;
        const AnimationClip& clip = clips_[p.clip_index];
        p.time += dt * p.speed;
        if (clip.duration > 1e-6f) {
            if (p.looping) {
                p.time = std::fmod(p.time, clip.duration);
                if (p.time < 0.0f)
                    p.time += clip.duration;
            } else {
                p.time = std::min(p.time, clip.duration);
            }
        }
        if (p.fade_duration > 1e-6f) {
            p.fade_age += dt;
            const float u = std::min(1.0f, p.fade_age / p.fade_duration);
            p.weight = p.fade_out ? (1.0f - u) : u;
        } else if (!p.fade_out) {
            p.weight = 1.0f;
        }
    }

    struct BlendSlot {
        uint32_t transform_index = TransformManager::kInvalid;
        uint32_t morph_index = ~0u;
        AnimationPath path = AnimationPath::Translation;
        float values[64]{};
        uint32_t comps = 0;
        float weight = 0.0f;
    };
    std::vector<BlendSlot> slots;
    slots.reserve(static_cast<size_t>(playing_n) * 64u);

    float tmp[64]{};
    for (const auto& p : players_) {
        if (!p.playing || p.clip_index >= clips_.size() || p.weight <= 1e-6f)
            continue;
        const AnimationClip& clip = clips_[p.clip_index];
        for (const AnimationChannel& ch : clip.channels) {
            if (channel_masked(ch) || ch.sampler_index >= clip.samplers.size())
                continue;
            const auto& samp = clip.samplers[ch.sampler_index];
            const uint32_t comps =
                std::min(64u, std::max(1u, samp.component_count));
            sample_channel(samp, p.time, tmp);

            BlendSlot* slot = nullptr;
            for (BlendSlot& s : slots) {
                if (s.path == ch.path && s.transform_index == ch.transform_index &&
                    s.morph_index == ch.morph_index) {
                    slot = &s;
                    break;
                }
            }
            if (!slot) {
                slots.push_back({});
                slot = &slots.back();
                slot->transform_index = ch.transform_index;
                slot->morph_index = ch.morph_index;
                slot->path = ch.path;
                slot->comps = comps;
                for (uint32_t c = 0; c < comps; ++c)
                    slot->values[c] = tmp[c];
                slot->weight = p.weight;
                continue;
            }
            const float w_sum = slot->weight + p.weight;
            const float t = (w_sum > 1e-8f) ? (p.weight / w_sum) : 1.0f;
            if (ch.path == AnimationPath::Rotation && comps >= 4) {
                glm::quat q0(slot->values[3], slot->values[0], slot->values[1],
                             slot->values[2]);
                glm::quat q1(tmp[3], tmp[0], tmp[1], tmp[2]);
                const glm::quat q = nlerp(q0, q1, t);
                slot->values[0] = q.x;
                slot->values[1] = q.y;
                slot->values[2] = q.z;
                slot->values[3] = q.w;
            } else {
                const uint32_t n = std::min(comps, slot->comps);
                for (uint32_t c = 0; c < n; ++c)
                    slot->values[c] = slot->values[c] + (tmp[c] - slot->values[c]) * t;
                if (comps > slot->comps) {
                    for (uint32_t c = slot->comps; c < comps; ++c)
                        slot->values[c] = tmp[c];
                    slot->comps = comps;
                }
            }
            slot->weight = w_sum;
        }
    }

    for (const BlendSlot& s : slots) {
        if (s.path == AnimationPath::Weights) {
            if (morphs && s.morph_index != kInvalidMorph)
                morphs->set_weights(s.morph_index, s.values, s.comps);
        } else {
            AnimationChannel ch{};
            ch.transform_index = s.transform_index;
            ch.morph_index = s.morph_index;
            ch.path = s.path;
            apply_channel(transforms, morphs, ch, s.values);
        }
    }

    finish_completed_fades();
}

void AnimationSystem::play_all_looping() {
    players_.clear();
    players_.reserve(clips_.size());
    for (uint32_t i = 0; i < clips_.size(); ++i) {
        AnimationPlayer p{};
        p.clip_index = i;
        p.time = 0.0f;
        p.speed = 1.0f;
        p.looping = true;
        p.playing = true;
        p.weight = 1.0f;
        players_.push_back(p);
        LOG_INFO("[Anim] Playing clip '" << clips_[i].name << "' duration="
                 << clips_[i].duration << "s channels=" << clips_[i].channels.size());
    }
}

bool AnimationSystem::play_exclusive(uint32_t clip_index, bool loop, float speed) {
    if (clip_index >= clips_.size())
        return false;
    players_.clear();
    AnimationPlayer p{};
    p.clip_index = clip_index;
    p.time = 0.0f;
    p.speed = speed;
    p.looping = loop;
    p.playing = true;
    p.weight = 1.0f;
    players_.push_back(p);
    LOG_INFO("[Anim] Exclusive play '" << clips_[clip_index].name << "' duration="
             << clips_[clip_index].duration << "s channels="
             << clips_[clip_index].channels.size()
             << " (" << (clip_index + 1) << "/" << clips_.size() << ")");
    return true;
}

bool AnimationSystem::crossfade(uint32_t clip_index, float fade_seconds, bool loop,
                                float speed) {
    if (clip_index >= clips_.size())
        return false;
    if (fade_seconds <= 1e-5f)
        return play_exclusive(clip_index, loop, speed);

    int incoming = -1;
    for (int i = static_cast<int>(players_.size()) - 1; i >= 0; --i) {
        if (players_[static_cast<size_t>(i)].playing &&
            !players_[static_cast<size_t>(i)].fade_out) {
            incoming = i;
            break;
        }
    }
    if (incoming >= 0 &&
        players_[static_cast<size_t>(incoming)].clip_index == clip_index)
        return true;

    std::vector<AnimationPlayer> kept;
    kept.reserve(2);
    if (incoming >= 0) {
        AnimationPlayer out = players_[static_cast<size_t>(incoming)];
        out.fade_out = true;
        out.fade_duration = fade_seconds;
        out.fade_age = 0.0f;
        out.weight = 1.0f;
        kept.push_back(out);
    }

    AnimationPlayer in{};
    in.clip_index = clip_index;
    in.time = 0.0f;
    in.speed = speed;
    in.looping = loop;
    in.playing = true;
    in.weight = 0.0f;
    in.fade_duration = fade_seconds;
    in.fade_age = 0.0f;
    in.fade_out = false;
    kept.push_back(in);
    players_.swap(kept);

    LOG_INFO("[Anim] Crossfade -> '" << clips_[clip_index].name << "' fade="
             << fade_seconds << "s (" << (clip_index + 1) << "/"
             << clips_.size() << ")");
    return true;
}

namespace {

bool name_ieq(const std::string& a, const char* b) {
    if (!b)
        return false;
    const size_t n = std::strlen(b);
    if (a.size() != n)
        return false;
    for (size_t i = 0; i < n; ++i) {
        const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
        const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (ca != cb)
            return false;
    }
    return true;
}

bool name_istarts(const std::string& a, const char* prefix) {
    if (!prefix)
        return false;
    const size_t n = std::strlen(prefix);
    if (a.size() < n)
        return false;
    for (size_t i = 0; i < n; ++i) {
        const char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
        const char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
        if (ca != cb)
            return false;
    }
    return true;
}

} // namespace

bool AnimationSystem::play_default_clip(bool loop, float speed) {
    if (clips_.empty())
        return false;

    // Prefix match so Walking_A / Run_Loop count (exact "Walk" was Fox-only).
    static const char* kPreferred[] = {"Walk", "Run", "Survey", "Idle", "T-Pose",
                                       "TPose", "Animation"};
    for (const char* pref : kPreferred) {
        const int idx = find_clip(pref);
        if (idx >= 0)
            return play_exclusive(static_cast<uint32_t>(idx), loop, speed);
    }
    return play_exclusive(0, loop, speed);
}

uint32_t AnimationSystem::cycle_next_clip(bool loop, float fade_seconds) {
    if (clips_.empty())
        return ~0u;
    int cur = active_clip_index();
    const uint32_t next =
        (cur < 0) ? 0u
                  : static_cast<uint32_t>((static_cast<uint32_t>(cur) + 1u) %
                                          clips_.size());
    crossfade(next, fade_seconds, loop, 1.0f);
    return next;
}

int AnimationSystem::active_clip_index() const {
    for (int i = static_cast<int>(players_.size()) - 1; i >= 0; --i) {
        const auto& p = players_[static_cast<size_t>(i)];
        if (p.playing && !p.fade_out && p.clip_index < clips_.size())
            return static_cast<int>(p.clip_index);
    }
    for (const auto& p : players_) {
        if (p.playing && p.clip_index < clips_.size())
            return static_cast<int>(p.clip_index);
    }
    return -1;
}

int AnimationSystem::find_clip(const char* name) const {
    if (!name || !name[0])
        return -1;
    for (uint32_t i = 0; i < clips_.size(); ++i) {
        if (name_ieq(clips_[i].name, name))
            return static_cast<int>(i);
    }
    for (uint32_t i = 0; i < clips_.size(); ++i) {
        if (name_istarts(clips_[i].name, name))
            return static_cast<int>(i);
    }
    return -1;
}

void AnimationSystem::ignore_transform_trs(uint32_t transform_index) {
    if (transform_index == TransformManager::kInvalid)
        return;
    for (ChannelMask& m : masks_) {
        if (m.transform_index == transform_index) {
            m.path_bits = 1u | 2u | 4u;
            return;
        }
    }
    masks_.push_back({transform_index, static_cast<uint8_t>(1u | 2u | 4u)});
}

void AnimationSystem::ignore_transform_translation(uint32_t transform_index) {
    if (transform_index == TransformManager::kInvalid)
        return;
    for (ChannelMask& m : masks_) {
        if (m.transform_index == transform_index) {
            m.path_bits |= 1u;
            return;
        }
    }
    masks_.push_back({transform_index, 1u});
}

bool AnimationSystem::play(uint32_t clip_index, bool loop, float speed) {
    if (clip_index >= clips_.size())
        return false;
    AnimationPlayer p{};
    p.clip_index = clip_index;
    p.time = 0.0f;
    p.speed = speed;
    p.looping = loop;
    p.playing = true;
    p.weight = 1.0f;
    players_.push_back(p);
    return true;
}

void AnimationSystem::stop_all() {
    for (auto& p : players_)
        p.playing = false;
}

} // namespace scene
