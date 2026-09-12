#include "gfx/GpuTimestamps.h"
#include "core/Log.h"
#include "core/Profiler.h"

#ifdef TRACY_ENABLE
#include <memory>
#include <tracy/TracyVulkan.hpp>
#endif

namespace gfx {
namespace {
#ifdef TRACY_ENABLE
tracy::VkCtx* g_tracy_vk = nullptr;
std::unique_ptr<tracy::VkCtxScope> g_tracy_gpu_zone[GpuTimestamps::kStageCount];

const tracy::SourceLocationData* tracy_gpu_loc(core::GpuStage s) {
    static const tracy::SourceLocationData k[] = {
        {"gpu.skin", "GpuTimestamps", "", 0, 0x26A69A},
        {"gpu.shadow", "GpuTimestamps", "", 0, 0x5C6BC0},
        {"gpu.cull", "GpuTimestamps", "", 0, 0x7E57C2},
        {"gpu.depth", "GpuTimestamps", "", 0, 0x78909C},
        {"gpu.opaque", "GpuTimestamps", "", 0, 0x43A047},
        {"gpu.trans", "GpuTimestamps", "", 0, 0x00897B},
        {"gpu.overlay", "GpuTimestamps", "", 0, 0xF9A825},
    };
    const int i = static_cast<int>(s);
    if (i < 0 || i >= static_cast<int>(core::GpuStage::Count))
        return &k[0];
    return &k[i];
}
#endif
} // namespace


bool GpuTimestamps::create(VkDevice device, VkPhysicalDevice phys) {
    destroy(device);
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys, &props);
    period_ns_ = props.limits.timestampPeriod;
    if (period_ns_ <= 0.0f || props.limits.timestampComputeAndGraphics == VK_FALSE) {
        LOG_INFO("[GpuTime] timestamps not supported (period="
                 << period_ns_ << ")");
        return false;
    }

    VkQueryPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    ci.queryCount = kMaxFrames * kQueriesPerFrame;
    if (vkCreateQueryPool(device, &ci, nullptr, &pool_) != VK_SUCCESS) {
        LOG_ERROR("[GpuTime] query pool create failed");
        pool_ = VK_NULL_HANDLE;
        return false;
    }
    wrote_ = {};
    primed_ = {};
    LOG_INFO("[GpuTime] timestamp queries ready (period=" << period_ns_ << " ns)");
    return true;
}

void GpuTimestamps::destroy(VkDevice device) {
#ifdef TRACY_ENABLE
    for (auto& z : g_tracy_gpu_zone)
        z.reset();
    if (g_tracy_vk) {
        TracyVkDestroy(g_tracy_vk);
        g_tracy_vk = nullptr;
    }
#endif
    if (pool_ != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    period_ns_ = 0.0f;
    wrote_ = {};
    primed_ = {};
}

void GpuTimestamps::init_tracy(VkPhysicalDevice phys, VkDevice device, VkQueue queue,
                               VkCommandPool pool) {
#ifdef TRACY_ENABLE
    if (g_tracy_vk || device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE ||
        pool == VK_NULL_HANDLE)
        return;
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &alloc, &cmd) != VK_SUCCESS || !cmd)
        return;
    g_tracy_vk = TracyVkContext(phys, device, queue, cmd);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    if (g_tracy_vk) {
        TracyVkContextName(g_tracy_vk, "AeroBoar", 8);
        LOG_INFO("[Tracy] Vulkan GPU context ready");
    } else {
        LOG_INFO("[Tracy] Vulkan GPU context failed");
    }
#else
    (void)phys;
    (void)device;
    (void)queue;
    (void)pool;
#endif
}

void GpuTimestamps::tracy_collect(VkCommandBuffer cmd) {
#ifdef TRACY_ENABLE
    if (g_tracy_vk && cmd != VK_NULL_HANDLE)
        TracyVkCollect(g_tracy_vk, cmd);
#else
    (void)cmd;
#endif
}

void GpuTimestamps::collect(VkDevice device, uint32_t frame, core::FrameStats& stats) {
    if (!pool_ || frame >= kMaxFrames || !primed_[frame])
        return;
    // Per-stage fetch: unwritten queries were reset but never written, so a
    // range WAIT on the whole slot would hang.
    bool any = false;
    float sum = 0.0f;
    for (uint32_t s = 0; s < kStageCount; ++s) {
        if (!wrote_[frame][s]) {
            stats.set_gpu_ms(static_cast<core::GpuStage>(s), -1.0f);
            continue;
        }
        uint64_t ticks[2] = {0, 0};
        const uint32_t first = frame * kQueriesPerFrame + s * 2u;
        const VkResult r = vkGetQueryPoolResults(
            device, pool_, first, 2, sizeof(ticks), ticks, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (r != VK_SUCCESS || ticks[1] <= ticks[0]) {
            stats.set_gpu_ms(static_cast<core::GpuStage>(s), -1.0f);
            continue;
        }
        const float ms =
            static_cast<float>(ticks[1] - ticks[0]) * period_ns_ * 1.0e-6f;
        stats.set_gpu_ms(static_cast<core::GpuStage>(s), ms);
        sum += ms;
        any = true;
    }
    stats.set_gpu_valid(any);
    stats.set_gpu_sum(sum);
}

void GpuTimestamps::cmd_reset(VkCommandBuffer cmd, uint32_t frame) {
    if (!pool_ || frame >= kMaxFrames)
        return;
    vkCmdResetQueryPool(cmd, pool_, frame * kQueriesPerFrame, kQueriesPerFrame);
    wrote_[frame].fill(0);
    // Reset is ALL_COMMANDS + TRANSFER_WRITE. TOP_OF_PIPE supports no access
    // flags (VUID-vkCmdPipelineBarrier-dstAccessMask-02816), so wait in
    // ALL_COMMANDS which covers later TOP/BOTTOM timestamp writes.
    VkMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0,
                         nullptr, 0, nullptr);
}

namespace {

// vkCmdWriteTimestamp takes a single stage bit (not a mask). Match the work
// so slices exclude TOP/BOTTOM idle. Mixed depth+HZB ends at BOTTOM_OF_PIPE.
VkPipelineStageFlagBits timestamp_begin_stage(core::GpuStage stage) {
    switch (stage) {
    case core::GpuStage::Skin:
    case core::GpuStage::Cull:
    case core::GpuStage::Shadow:
    case core::GpuStage::DepthHzb:
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case core::GpuStage::Opaque:
    case core::GpuStage::Transparent:
        return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    case core::GpuStage::Overlay:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    default:
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
}

VkPipelineStageFlagBits timestamp_end_stage(core::GpuStage stage) {
    switch (stage) {
    case core::GpuStage::Skin:
    case core::GpuStage::Cull:
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case core::GpuStage::Shadow:
    case core::GpuStage::DepthHzb:
        return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    case core::GpuStage::Opaque:
    case core::GpuStage::Transparent:
    case core::GpuStage::Overlay:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    default:
        return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
}

} // namespace

void GpuTimestamps::write_begin(VkCommandBuffer cmd, uint32_t frame,
                                core::GpuStage stage) {
#ifdef TRACY_ENABLE
    if (g_tracy_vk && cmd != VK_NULL_HANDLE) {
        const uint32_t si = static_cast<uint32_t>(stage);
        if (si < kStageCount)
            g_tracy_gpu_zone[si] = std::make_unique<tracy::VkCtxScope>(
                g_tracy_vk, tracy_gpu_loc(stage), cmd, true);
    }
#endif
    if (!pool_ || frame >= kMaxFrames)
        return;
    vkCmdWriteTimestamp(cmd, timestamp_begin_stage(stage), pool_,
                        index(frame, stage, false));
}

void GpuTimestamps::write_end(VkCommandBuffer cmd, uint32_t frame,
                              core::GpuStage stage) {
#ifdef TRACY_ENABLE
    const uint32_t si = static_cast<uint32_t>(stage);
    if (si < kStageCount)
        g_tracy_gpu_zone[si].reset();
#endif
    if (!pool_ || frame >= kMaxFrames)
        return;
    vkCmdWriteTimestamp(cmd, timestamp_end_stage(stage), pool_,
                        index(frame, stage, true));
    wrote_[frame][static_cast<uint32_t>(stage)] = 1;
}

} // namespace gfx
