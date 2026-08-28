#pragma once

#include "core/FrameStats.h"
#include <array>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace gfx {

// Per-FIF timestamp query pool. After the slot's fence, collect() converts
// ticks to milliseconds. Stages that were not written this slot stay < 0.
class GpuTimestamps {
  public:
    static constexpr uint32_t kMaxFrames = 2;
    static constexpr uint32_t kStageCount =
        static_cast<uint32_t>(core::GpuStage::Count);
    static constexpr uint32_t kQueriesPerFrame = kStageCount * 2u;

    bool create(VkDevice device, VkPhysicalDevice phys);
    void destroy(VkDevice device);

    // Call after the FIF fence succeeded (previous submit finished).
    void collect(VkDevice device, uint32_t frame, core::FrameStats& stats);

    // Start of command buffer: reset this slot's queries.
    void cmd_reset(VkCommandBuffer cmd, uint32_t frame);

    void write_begin(VkCommandBuffer cmd, uint32_t frame, core::GpuStage stage);
    void write_end(VkCommandBuffer cmd, uint32_t frame, core::GpuStage stage);

    // After a successful submit of this FIF slot, so the next wait can collect.
    void mark_submitted(uint32_t frame) {
        if (frame < kMaxFrames)
            primed_[frame] = 1;
    }

    [[nodiscard]] bool is_ready() const { return pool_ != VK_NULL_HANDLE; }

    // RAII begin/end timestamps. Unused stages are left unwritten (HUD shows --).
    class Scope {
      public:
        Scope(GpuTimestamps& t, VkCommandBuffer cmd, uint32_t frame,
              core::GpuStage stage)
            : t_(&t), cmd_(cmd), frame_(frame), stage_(stage) {
            t_->write_begin(cmd_, frame_, stage_);
        }
        Scope(Scope&& o) noexcept
            : t_(o.t_), cmd_(o.cmd_), frame_(o.frame_), stage_(o.stage_) {
            o.t_ = nullptr;
        }
        ~Scope() {
            if (t_)
                t_->write_end(cmd_, frame_, stage_);
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope& operator=(Scope&&) = delete;

      private:
        GpuTimestamps* t_ = nullptr;
        VkCommandBuffer cmd_{};
        uint32_t frame_{};
        core::GpuStage stage_{};
    };

    [[nodiscard]] Scope scope(VkCommandBuffer cmd, uint32_t frame,
                              core::GpuStage stage) {
        return Scope(*this, cmd, frame, stage);
    }

  private:
    static uint32_t index(uint32_t frame, core::GpuStage stage, bool end) {
        return frame * kQueriesPerFrame +
               static_cast<uint32_t>(stage) * 2u + (end ? 1u : 0u);
    }

    VkQueryPool pool_ = VK_NULL_HANDLE;
    float period_ns_ = 0.0f;
    std::array<std::array<uint8_t, kStageCount>, kMaxFrames> wrote_{};
    std::array<uint8_t, kMaxFrames> primed_{};
};

} // namespace gfx
