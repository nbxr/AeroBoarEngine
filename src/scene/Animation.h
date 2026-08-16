#pragma once

#include "scene/TransformManager.h"
#include <cstdint>
#include <string>
#include <vector>

namespace tinygltf {
class Model;
}

namespace scene {

class MorphSystem;

enum class AnimationPath : uint8_t {
    Translation = 0,
    Rotation    = 1,
    Scale       = 2,
    Weights     = 3, // morph target weights (glTF path "weights")
};

enum class AnimationInterp : uint8_t {
    Step        = 0,
    Linear      = 1,
    CubicSpline = 2,
};

struct AnimationSamplerData {
    AnimationInterp interp = AnimationInterp::Linear;
    uint32_t component_count = 3; // 3 = vec3, 4 = quat
    std::vector<float> times;
    std::vector<float> values; // times.size() * component_count (or *3 for cubic)
};

struct AnimationChannel {
    uint32_t transform_index = TransformManager::kInvalid; // TRS targets
    uint32_t morph_index = ~0u; // Weights path → MorphSystem instance
    uint32_t sampler_index = 0;
    AnimationPath path = AnimationPath::Translation;
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationSamplerData> samplers;
    std::vector<AnimationChannel> channels;
};

struct AnimationPlayer {
    uint32_t clip_index = 0;
    float time = 0.0f;
    float speed = 1.0f;
    bool looping = true;
    bool playing = false;
    float weight = 1.0f;
    float fade_duration = 0.0f;
    float fade_age = 0.0f;
    bool fade_out = false;
};

// Scene-owned animation clips + players. Sample writes TransformManager locals
// (TRS components); call Engine::sync_scene_transforms after update.
class AnimationSystem {
  public:
    void clear();

    // Build clips from tinygltf; resolve channel targets via gltf_node_to_transform.
    // Optional morphs: enables path "weights" channels.
    // Returns number of clips loaded.
    uint32_t load_from_gltf(const tinygltf::Model& model,
                            const std::vector<uint32_t>& gltf_node_to_transform,
                            const MorphSystem* morphs = nullptr);

    // Advance all playing players and apply samples to transforms (+ morph weights).
    void update(float delta_time, TransformManager& transforms,
                MorphSystem* morphs = nullptr);

    // Start every clip looping simultaneously (rare — multi-clip assets fight).
    void play_all_looping();

    // Stop others and play one clip (preferred for Fox Walk/Run/Survey, etc.).
    bool play_exclusive(uint32_t clip_index, bool loop = true, float speed = 1.0f);

    // Fade from the current incoming clip to clip_index. fade_seconds <= 0 is exclusive.
    bool crossfade(uint32_t clip_index, float fade_seconds, bool loop = true,
                   float speed = 1.0f);

    // Prefer Walk* → Run* → Idle/T-Pose → clip 0.
    bool play_default_clip(bool loop = true, float speed = 1.0f);

    // Cycle to next clip (wraps) via crossfade. Returns new clip index or ~0.
    uint32_t cycle_next_clip(bool loop = true, float fade_seconds = 0.2f);

    // Skip channels on a gameplay-owned transform (player object TRS, or
    // skeleton-root translation so Walk/Run stay in place).
    void ignore_transform_trs(uint32_t transform_index);
    void ignore_transform_translation(uint32_t transform_index);

    // Case-insensitive exact name, else first clip whose name starts with name.
    [[nodiscard]] int find_clip(const char* name) const;

    [[nodiscard]] uint32_t clip_count() const {
        return static_cast<uint32_t>(clips_.size());
    }
    [[nodiscard]] const AnimationClip& clip(uint32_t i) const { return clips_[i]; }
    [[nodiscard]] const std::vector<AnimationPlayer>& players() const { return players_; }
    [[nodiscard]] int active_clip_index() const; // incoming clip, or -1

    bool play(uint32_t clip_index, bool loop = true, float speed = 1.0f);
    void stop_all();

  private:
    static void sample_channel(const AnimationSamplerData& samp, float time,
                               float* out_components);
    static void apply_channel(TransformManager& transforms, MorphSystem* morphs,
                              const AnimationChannel& ch, const float* values);
    [[nodiscard]] bool channel_masked(const AnimationChannel& ch) const;
    void finish_completed_fades();

    struct ChannelMask {
        uint32_t transform_index = TransformManager::kInvalid;
        uint8_t path_bits = 0; // 1=T 2=R 4=S
    };

    std::vector<AnimationClip> clips_{};
    std::vector<AnimationPlayer> players_{};
    std::vector<ChannelMask> masks_{};
};

} // namespace scene
