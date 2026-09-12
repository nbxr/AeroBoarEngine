#pragma once

#include "core/Profiler.h"
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace core {

// Named CPU slices for the HUD (and Tracy zones when AERO_TRACY=ON).
enum class CpuStage : uint8_t {
    Input = 0,
    Simulate, // move + locomotion + animation + scripts
    Physics,
    GpuWait,  // fence / acquire
    Uploads,  // worlds[], lights, morph/skin buffer writes
    Record,   // command buffer record
    Present,  // queue submit + present
    Count
};

// GPU slices filled by timestamp queries (see gfx::GpuTimestamps).
enum class GpuStage : uint8_t {
    Skin = 0,
    Shadow,
    Cull,
    DepthHzb,
    Opaque,
    Transparent,
    Overlay,
    Count
};

inline const char* cpu_stage_name(CpuStage s) {
    switch (s) {
    case CpuStage::Input:
        return "input";
    case CpuStage::Simulate:
        return "simulate";
    case CpuStage::Physics:
        return "physics";
    case CpuStage::GpuWait:
        return "gpu-wait";
    case CpuStage::Uploads:
        return "uploads";
    case CpuStage::Record:
        return "record";
    case CpuStage::Present:
        return "present";
    default:
        return "?";
    }
}

inline const char* gpu_stage_name(GpuStage s) {
    switch (s) {
    case GpuStage::Skin:
        return "skin";
    case GpuStage::Shadow:
        return "shadow";
    case GpuStage::Cull:
        return "cull";
    case GpuStage::DepthHzb:
        return "depth";
    case GpuStage::Opaque:
        return "opaque";
    case GpuStage::Transparent:
        return "trans";
    case GpuStage::Overlay:
        return "overlay";
    default:
        return "?";
    }
}

// last_ms < 0 → unused this frame (HUD --). avg/min/max ignore those pushes.
// Extrema snap back to avg every 90 samples so they track recent frames.
struct TimeSample {
    float last_ms = -1.0f;
    float avg_ms = 0.0f;
    float min_ms = 0.0f;
    float max_ms = 0.0f;
    uint32_t n = 0;

    [[nodiscard]] bool valid() const { return last_ms >= 0.0f; }
};

inline void time_sample_push(TimeSample& s, float ms) {
    s.last_ms = ms;
    if (ms < 0.0f)
        return;
    if (s.n == 0) {
        s.avg_ms = s.min_ms = s.max_ms = ms;
        s.n = 1;
        return;
    }
    s.avg_ms = s.avg_ms * 0.9f + ms * 0.1f;
    s.min_ms = std::min(s.min_ms, ms);
    s.max_ms = std::max(s.max_ms, ms);
    ++s.n;
    if (s.n % 90u == 0u)
        s.min_ms = s.max_ms = s.avg_ms;
}

struct FrameSnapshot {
    TimeSample cpu[static_cast<int>(CpuStage::Count)]{};
    TimeSample gpu[static_cast<int>(GpuStage::Count)]{};
    TimeSample cpu_frame{};
    TimeSample cpu_busy{}; // all CPU stages except gpu-wait
    TimeSample cpu_wait{}; // gpu-wait
    TimeSample cpu_other{}; // wall - sum(stages)
    TimeSample gpu_sum{};
    bool gpu_valid = false;
    uint32_t vis = 0;
    uint32_t total = 0;
    bool hzb = false;
    uint32_t ml_vis = 0;
    uint32_t ml_total = 0;
    bool meshlet = false;
};

class FrameStats {
  public:
    bool hud_enabled = true;

    void begin_cpu_frame() {
        for (int i = 0; i < static_cast<int>(CpuStage::Count); ++i)
            cpu_ms_[i] = 0.0f;
        frame_t0_ = Clock::now();
    }

    void end_cpu_frame() {
        const float wall = ms_since(frame_t0_);
        float busy = 0.0f;
        float wait = 0.0f;
        float accounted = 0.0f;
        for (int i = 0; i < static_cast<int>(CpuStage::Count); ++i) {
            time_sample_push(snapshot_.cpu[i], cpu_ms_[i]);
            accounted += cpu_ms_[i];
            if (i == static_cast<int>(CpuStage::GpuWait))
                wait += cpu_ms_[i];
            else
                busy += cpu_ms_[i];
        }
        float other = wall - accounted;
        if (other < 0.0f)
            other = 0.0f;
        time_sample_push(snapshot_.cpu_frame, wall);
        time_sample_push(snapshot_.cpu_busy, busy);
        time_sample_push(snapshot_.cpu_wait, wait);
        time_sample_push(snapshot_.cpu_other, other);
    }

    void set_gpu_ms(GpuStage s, float ms) {
        time_sample_push(snapshot_.gpu[static_cast<int>(s)], ms);
    }
    void set_gpu_valid(bool v) { snapshot_.gpu_valid = v; }
    void set_gpu_sum(float ms) {
        if (!snapshot_.gpu_valid)
            time_sample_push(snapshot_.gpu_sum, -1.0f);
        else
            time_sample_push(snapshot_.gpu_sum, ms);
    }
    void set_cull(uint32_t vis, uint32_t total, bool hzb) {
        snapshot_.vis = vis;
        snapshot_.total = total;
        snapshot_.hzb = hzb;
    }
    void set_meshlet_cull(uint32_t vis, uint32_t total, bool active) {
        snapshot_.ml_vis = vis;
        snapshot_.ml_total = total;
        snapshot_.meshlet = active;
    }

    [[nodiscard]] const FrameSnapshot& snapshot() const { return snapshot_; }

    class CpuScope {
      public:
        CpuScope(FrameStats& stats, CpuStage stage)
            : stats_(&stats), stage_(stage), t0_(Clock::now()) {
#ifdef TRACY_ENABLE
            tracy_ctx_ = ___tracy_emit_zone_begin(tracy_cpu_loc(stage), 1);
            tracy_on_ = true;
#endif
        }
        CpuScope(CpuScope&& o) noexcept
            : stats_(o.stats_), stage_(o.stage_), t0_(o.t0_)
#ifdef TRACY_ENABLE
              ,
              tracy_ctx_(o.tracy_ctx_), tracy_on_(o.tracy_on_)
#endif
        {
            o.stats_ = nullptr;
#ifdef TRACY_ENABLE
            o.tracy_on_ = false;
#endif
        }
        ~CpuScope() {
#ifdef TRACY_ENABLE
            if (tracy_on_)
                ___tracy_emit_zone_end(tracy_ctx_);
#endif
            if (stats_)
                stats_->cpu_ms_[static_cast<int>(stage_)] += ms_since(t0_);
        }
        CpuScope(const CpuScope&) = delete;
        CpuScope& operator=(const CpuScope&) = delete;
        CpuScope& operator=(CpuScope&&) = delete;

      private:
#ifdef TRACY_ENABLE
        static const ___tracy_source_location_data* tracy_cpu_loc(CpuStage s) {
            static const ___tracy_source_location_data k[] = {
                {"cpu.input", "CpuScope", "", 0, 0x42A5F5},
                {"cpu.simulate", "CpuScope", "", 0, 0x66BB6A},
                {"cpu.physics", "CpuScope", "", 0, 0xFFA726},
                {"cpu.gpu-wait", "CpuScope", "", 0, 0xEF5350},
                {"cpu.uploads", "CpuScope", "", 0, 0xAB47BC},
                {"cpu.record", "CpuScope", "", 0, 0x26C6DA},
                {"cpu.present", "CpuScope", "", 0, 0x8D6E63},
            };
            const int i = static_cast<int>(s);
            if (i < 0 || i >= static_cast<int>(CpuStage::Count))
                return &k[0];
            return &k[i];
        }
        TracyCZoneCtx tracy_ctx_{};
        bool tracy_on_ = false;
#endif
        FrameStats* stats_ = nullptr;
        CpuStage stage_{};
        std::chrono::high_resolution_clock::time_point t0_{};
    };

    [[nodiscard]] CpuScope scope(CpuStage s) { return CpuScope(*this, s); }

  private:
    using Clock = std::chrono::high_resolution_clock;
    static float ms_since(Clock::time_point t0) {
        return std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
    }

    float cpu_ms_[static_cast<int>(CpuStage::Count)]{};
    Clock::time_point frame_t0_{};
    FrameSnapshot snapshot_{};
};

} // namespace core
