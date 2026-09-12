#include "gfx/Engine.h"
#include "gfx/Depth.h"
#include "gfx/Renderer.h"
#include "core/Frustum.h"
#include <vulkan/vulkan.h>
#include "gfx/VulkanContext.h"
#include "gfx/PassContext.h"
#include "gfx/Light.h"
#include "gfx/PbrPush.h"
#include "core/Log.h"
#include "core/FrameStats.h"
#include "core/Profiler.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

void record_indirect_draws(VkCommandBuffer cmd, gfx::Renderer& renderer,
                           VkPipeline pipeline, const glm::mat4& viewProj,
                           gfx::CullPass pass = gfx::CullPass::Opaque,
                           VkExtent2D extent = {0, 0}) {
    auto& vk = renderer.vk;
    if (extent.width == 0 || extent.height == 0)
        extent = vk.swap_chain_extent;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 0, 1,
                            &vk.bindless_descriptor_sets[renderer.current_frame], 0, nullptr);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    auto& index_buf = renderer.mesh_manager.get_render_index_buffer();
    vkCmdBindIndexBuffer(cmd, index_buf.buffer, 0, VK_INDEX_TYPE_UINT32);

    auto& vertex_buf = renderer.mesh_manager.get_render_vertex_buffer();
    VkDeviceSize vbo_offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buf.buffer, &vbo_offset);

    gfx::PbrPush pushData{};
    pushData.viewProj = viewProj;
    pushData.extra = glm::uvec4{0u, 0u, 0u, 0u};

    vkCmdPushConstants(cmd, vk.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(gfx::PbrPush), &pushData);

    renderer.gpu_culling.cmd_draw_indexed(cmd, renderer.current_frame, pass);
}

enum class HudTimeUnit { Ns, Us, Ms };

HudTimeUnit pick_hud_unit(float max_ms) {
    if (max_ms < 0.0f)
        return HudTimeUnit::Us;
    if (max_ms < 0.001f)
        return HudTimeUnit::Ns;
    if (max_ms < 1.0f)
        return HudTimeUnit::Us;
    return HudTimeUnit::Ms;
}

const char* hud_unit_suffix(HudTimeUnit u) {
    switch (u) {
    case HudTimeUnit::Ns:
        return "ns";
    case HudTimeUnit::Us:
        return "us";
    default:
        return "ms";
    }
}

void fmt_hud_time(char* buf, size_t n, float ms, HudTimeUnit u, bool valid) {
    if (!valid || ms < 0.0f) {
        std::snprintf(buf, n, "     --");
        return;
    }
    switch (u) {
    case HudTimeUnit::Ns:
        std::snprintf(buf, n, "%7.0f", ms * 1.0e6f);
        break;
    case HudTimeUnit::Us:
        std::snprintf(buf, n, "%7.1f", ms * 1.0e3f);
        break;
    default:
        std::snprintf(buf, n, "%7.2f", ms);
        break;
    }
}

void fmt_hud_range(char* buf, size_t n, const core::TimeSample& s, HudTimeUnit u) {
    if (!s.valid() || s.n < 2) {
        buf[0] = '\0';
        return;
    }
    char a[16];
    char b[16];
    fmt_hud_time(a, sizeof(a), s.min_ms, u, true);
    fmt_hud_time(b, sizeof(b), s.max_ms, u, true);
    std::snprintf(buf, n, "%s-%s", a, b);
}

void hud_sample_line(gfx::HudTextPass& hud, float x, float y, float size,
                     const glm::vec4& color, const char* name,
                     const core::TimeSample& s, HudTimeUnit u) {
    char avg[16];
    char range[40];
    char line[96];
    fmt_hud_time(avg, sizeof(avg), s.avg_ms, u, s.valid());
    fmt_hud_range(range, sizeof(range), s, u);
    if (range[0])
        std::snprintf(line, sizeof(line), "  %-8s %s %s %s", name, avg, range,
                      hud_unit_suffix(u));
    else
        std::snprintf(line, sizeof(line), "  %-8s %s %s", name, avg,
                      hud_unit_suffix(u));
    hud.add_text(line, x, y, size, color);
}

void fill_frame_stats_hud(gfx::HudTextPass& hud, const core::FrameSnapshot& snap) {
    if (!hud.is_ready())
        return;
    const glm::vec4 head(0.95f, 0.95f, 0.90f, 0.92f);
    const glm::vec4 cpu_c(0.85f, 0.90f, 0.55f, 0.90f);
    const glm::vec4 gpu_c(0.55f, 0.88f, 0.95f, 0.90f);
    const glm::vec4 dim(0.70f, 0.70f, 0.68f, 0.82f);
    const float x = 12.0f;
    const float size = 14.0f;
    float y = 10.0f;
    const float dy = 15.0f;
    char line[96];
    char busy_s[16];
    char wait_s[16];
    char wall_s[16];

    const float wall = snap.cpu_frame.last_ms;
    const float fps = (snap.cpu_frame.valid() && wall > 0.05f) ? (1000.0f / wall)
                                                              : 0.0f;
    const float fps_avg =
        (snap.cpu_frame.n > 0 && snap.cpu_frame.avg_ms > 0.05f)
            ? (1000.0f / snap.cpu_frame.avg_ms)
            : 0.0f;
    fmt_hud_time(wall_s, sizeof(wall_s), wall, HudTimeUnit::Ms,
                 snap.cpu_frame.valid());
    std::snprintf(line, sizeof(line), "FPS %5.0f avg %5.0f %s ms", fps, fps_avg,
                  wall_s);
    hud.add_text(line, x, y, size, head);
    y += dy;

    fmt_hud_time(busy_s, sizeof(busy_s), snap.cpu_busy.avg_ms, HudTimeUnit::Ms,
                 snap.cpu_busy.valid());
    fmt_hud_time(wait_s, sizeof(wait_s), snap.cpu_wait.avg_ms, HudTimeUnit::Ms,
                 snap.cpu_wait.valid());
    std::snprintf(line, sizeof(line), "busy %s ms  wait %s ms", busy_s, wait_s);
    hud.add_text(line, x, y, size, cpu_c);
    y += dy;

    float cpu_max = 0.0f;
    for (int i = 0; i < static_cast<int>(core::CpuStage::Count); ++i) {
        if (i == static_cast<int>(core::CpuStage::GpuWait))
            continue;
        if (snap.cpu[i].valid())
            cpu_max = std::max(cpu_max, snap.cpu[i].avg_ms);
    }
    if (snap.cpu_other.valid())
        cpu_max = std::max(cpu_max, snap.cpu_other.avg_ms);
    const HudTimeUnit cpu_u = pick_hud_unit(cpu_max);
    std::snprintf(line, sizeof(line), "cpu n-1  avg  min-max");
    hud.add_text(line, x, y, size, cpu_c);
    y += dy;
    for (int i = 0; i < static_cast<int>(core::CpuStage::Count); ++i) {
        const HudTimeUnit u = (i == static_cast<int>(core::CpuStage::GpuWait))
                                  ? HudTimeUnit::Ms
                                  : cpu_u;
        hud_sample_line(hud, x, y, size, dim,
                        core::cpu_stage_name(static_cast<core::CpuStage>(i)),
                        snap.cpu[i], u);
        y += dy;
    }
    hud_sample_line(hud, x, y, size, dim, "other", snap.cpu_other, cpu_u);
    y += dy;

    float gpu_max = 0.0f;
    for (int i = 0; i < static_cast<int>(core::GpuStage::Count); ++i) {
        if (snap.gpu[i].valid())
            gpu_max = std::max(gpu_max, snap.gpu[i].avg_ms);
    }
    const HudTimeUnit gpu_u = pick_hud_unit(gpu_max);
    if (snap.gpu_valid && snap.gpu_sum.valid()) {
        char gsum[16];
        fmt_hud_time(gsum, sizeof(gsum), snap.gpu_sum.avg_ms, gpu_u, true);
        std::snprintf(line, sizeof(line), "gpu n-2  %s %s  avg  min-max", gsum,
                      hud_unit_suffix(gpu_u));
    } else {
        std::snprintf(line, sizeof(line), "gpu n-2  --");
    }
    hud.add_text(line, x, y, size, gpu_c);
    y += dy;
    for (int i = 0; i < static_cast<int>(core::GpuStage::Count); ++i) {
        hud_sample_line(hud, x, y, size, dim,
                        core::gpu_stage_name(static_cast<core::GpuStage>(i)),
                        snap.gpu[i], gpu_u);
        y += dy;
    }

    if (snap.total > 0) {
        std::snprintf(line, sizeof(line), "vis %u/%u  %s", snap.vis, snap.total,
                      snap.hzb ? "hzb" : "frustum");
    } else {
        std::snprintf(line, sizeof(line), "vis --");
    }
    hud.add_text(line, x, y, size, head);
    y += dy;
    if (snap.meshlet && snap.ml_total > 0) {
        std::snprintf(line, sizeof(line), "ml %u/%u  cone", snap.ml_vis,
                      snap.ml_total);
    } else {
        std::snprintf(line, sizeof(line), "ml --");
    }
    hud.add_text(line, x, y, size, head);
}

} // namespace

static_assert(gfx::GpuTimestamps::kMaxFrames >= gfx::Renderer::MAX_FRAMES_IN_FLIGHT,
              "GpuTimestamps FIF must cover Renderer::MAX_FRAMES_IN_FLIGHT");

void gfx::Engine::render() {
    auto& vk = renderer.vk;
    auto& frame = renderer.frames[renderer.current_frame];

    struct EndCpuFrame {
        core::FrameStats& stats;
        ~EndCpuFrame() { stats.end_cpu_frame(); }
    } end_cpu{frame_stats};

    if (vk.device_lost)
        return;

    poll_display_composition();
    if (vk.device_lost)
        return;

    uint32_t image_index = 0;
    VkResult result = VK_SUCCESS;
    {
        auto gpu_wait = frame_stats.scope(core::CpuStage::GpuWait);

        // Finite wait: if the GPU is stuck (TDR about to fire / DWM drop) don't
        // block the main loop forever. Happy path still returns immediately.
        constexpr uint64_t kFenceTimeoutNs = 1000000000ull; // 1s
        VkResult wait_res = vkWaitForFences(vk.device, 1, &frame.in_flight_fence,
                                            VK_TRUE, kFenceTimeoutNs);
        if (wait_res == VK_TIMEOUT) {
            static uint32_t timeout_logs = 0;
            if (timeout_logs < 4) {
                ++timeout_logs;
                LOG_INFO("[Vulkan] GPU fence wait timed out (1s) — skipping frame");
            }
            return; // fence still in flight; do not reset or submit
        }
        if (wait_res == VK_ERROR_DEVICE_LOST) {
            mark_device_lost("vkWaitForFences");
            return;
        } else if (wait_res != VK_SUCCESS) {
            LOG_ERROR("vkWaitForFences failed with VkResult=" << (int)wait_res);
            return;
        }

        // Previous GPU work for this FIF slot is done — timestamps + cull counts.
        gpu_times.collect(vk.device, renderer.current_frame, frame_stats);

        // Previous GPU cull results for this frame slot are still in counts[] until
        // the next record() zeros them — log cull stats on change.
        // Skip the first read after load (fence starts signaled → counts still zero).
        if (renderer.gpu_culling.is_ready()) {
            const uint32_t visible =
                renderer.gpu_culling.read_visible_count(renderer.current_frame);
            const uint32_t total = renderer.last_total_render_meshes;
            const bool hzb = renderer.last_cull_used_hzb[renderer.current_frame];
            frame_stats.set_cull(visible, total, hzb);
            const auto ml = renderer.gpu_culling.read_meshlet_stats(
                renderer.current_frame);
            frame_stats.set_meshlet_cull(ml.drawn, ml.tested, ml.active);
            static uint32_t frames_with_cull = 0;
            if (total > 0) {
                ++frames_with_cull;
                if (frames_with_cull > Renderer::MAX_FRAMES_IN_FLIGHT) {
                    const uint32_t culled = (total > visible) ? (total - visible) : 0u;
                    static uint32_t prev_culled = ~0u;
                    static uint32_t prev_total = ~0u;
                    static bool prev_hzb = false;
                    if (culled != prev_culled || total != prev_total || hzb != prev_hzb) {
                        prev_culled = culled;
                        prev_total = total;
                        prev_hzb = hzb;
                        LOG_INFO("[Cull] " << culled << " of " << total
                                 << " objects culled (" << visible << " drawn)"
                                 << (hzb ? " [hzb=on]" : " [hzb=off]"));
                    }
                    renderer.last_visible_instances = visible;
                    TracyPlot("cull.vis", static_cast<int64_t>(visible));
                    TracyPlot("cull.total", static_cast<int64_t>(total));
                    if (ml.active) {
                        TracyPlot("meshlet.drawn", static_cast<int64_t>(ml.drawn));
                        TracyPlot("meshlet.tested", static_cast<int64_t>(ml.tested));
                    }
                    if (ml.active && ml.tested > 0) {
                        const uint32_t ml_culled =
                            (ml.tested > ml.drawn) ? (ml.tested - ml.drawn) : 0u;
                        static uint32_t prev_ml_c = ~0u;
                        static uint32_t prev_ml_t = ~0u;
                        if (ml_culled != prev_ml_c || ml.tested != prev_ml_t) {
                            prev_ml_c = ml_culled;
                            prev_ml_t = ml.tested;
                            LOG_INFO("[Meshlet] " << ml_culled << " of " << ml.tested
                                     << " clusters culled (" << ml.drawn
                                     << " drawn)");
                        }
                    }
                }
            }
        }

        result = vkAcquireNextImageKHR(
            vk.device,
            vk.swapchain,
            UINT64_MAX,
            vk.image_available_semaphores[renderer.current_frame],
            VK_NULL_HANDLE,
            &image_index
        );
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    } else if (result == VK_ERROR_SURFACE_LOST_KHR) {
        mark_device_lost("vkAcquireNextImageKHR SURFACE_LOST");
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        if (result == VK_ERROR_DEVICE_LOST) {
            mark_device_lost("vkAcquireNextImageKHR");
        } else {
            LOG_ERROR("Failed to acquire swapchain image (VkResult=" << (int)result << ")");
        }
        return;
    }

    {
        auto& hud = renderer.hud_text;
        hud.begin_frame();
        if (frame_stats.hud_enabled)
            fill_frame_stats_hud(hud, frame_stats.snapshot());
    }

    vkResetFences(vk.device, 1, &frame.in_flight_fence);
    vkResetCommandBuffer(frame.command_buffer, 0);

    float aspect = (float)vk.swap_chain_extent.width / (float)vk.swap_chain_extent.height;
    glm::mat4 view = camera.get_view_matrix();
    glm::mat4 proj = camera.get_projection_matrix(aspect);
    proj[1][1] *= -1.0f;
    glm::mat4 viewProj = proj * view;

    const uint32_t fi = renderer.current_frame;
    const bool can_cull = renderer.gpu_culling.is_ready();
    bool gpu_transparent = false;
    bool cpu_transparent = false;
    bool can_hzb = false;

    {
        auto uploads = frame_stats.scope(core::CpuStage::Uploads);
        // After this frame slot's fence wait: safe to rewrite its GPU cull models.
        sync_scene_transforms();

        const bool have_transparents =
            can_cull && renderer.gpu_culling.transparent_item_count() > 0 &&
            renderer.gpu_culling.has_transparent_half();
        gpu_transparent = have_transparents && renderer.transparent.wboit_ready();
        cpu_transparent = have_transparents && !gpu_transparent;
        if (cpu_transparent) {
            const core::Frustum fr = core::Frustum::from_view_proj(viewProj);
            renderer.transparent.collect_and_sort(
                renderer.scene_manager, renderer.material_manager,
                renderer.mesh_manager, fr, camera.get_position());
            renderer.transparent.upload_instances(renderer.gpu_culling, fi);
        } else {
            renderer.transparent.clear_items();
        }
        write_frame_lighting(renderer.current_frame, &viewProj);
    }

    can_hzb = occlusion_cull_enabled_ && can_cull &&
              renderer.hzb.is_ready() &&
              fi < renderer.depth_prepass.framebuffers.size() &&
              fi < renderer.depth_prepass.depth_images.size() &&
              renderer.vk.depth_prepass_pipeline != VK_NULL_HANDLE;

    {
    auto record = frame_stats.scope(core::CpuStage::Record);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(frame.command_buffer, &begin_info) != VK_SUCCESS) {
        LOG_ERROR("Failed to begin command buffer");
        return;
    }

    gpu_times.cmd_reset(frame.command_buffer, fi);

    // ------------------------------------------------------------------
    // Compute culls stay outside render passes (Adreno: no mid-RP barriers).
    //   [hzb] frustum opaque → prepass → HZB → frustum+HZB opaque [+ transparent]
    //   [default] one frustum opaque cull; WBOIT transparents are a second emit
    // Instance SSBO is bound at build_scene — not UPDATE_AFTER_BIND here.
    // ------------------------------------------------------------------
    if (renderer.scene_manager.skins().gpu_compute_ready() &&
        renderer.gpu_culling.is_ready()) {
        auto skin = gpu_times.scope(frame.command_buffer, fi, core::GpuStage::Skin);
        renderer.scene_manager.skins().record(
            frame.command_buffer, fi, renderer.gpu_culling.worlds(fi),
            renderer.gpu_culling.world_count());
    }

    const bool do_shadow = renderer.shadow_map.last_on &&
                           renderer.shadow_map.is_ready() &&
                           vk.shadow_pipeline != VK_NULL_HANDLE && can_cull;
    if (do_shadow) {
        auto shadow = gpu_times.scope(frame.command_buffer, fi, core::GpuStage::Shadow);
        glm::vec3 full_corners[8];
        const bool have_corners =
            gfx::ShadowMap::frustum_corners(viewProj, full_corners);
        const glm::vec4 splits = renderer.shadow_map.last_splits;
        const float npl = std::max(camera.near_plane, 0.01f);
        const float s0 = splits.x > 0.0f ? splits.x : npl + (camera.far_plane - npl) / 3.0f;
        const float s1 = splits.y > 0.0f ? splits.y : npl + 2.0f * (camera.far_plane - npl) / 3.0f;
        const float fpl = splits.z > s1 ? splits.z : std::max(camera.far_plane, s1 + 1.0f);
        const float edges[4] = {npl, s0, s1, fpl};
        auto t_of = [&](float d) {
            const float f = std::max(edges[3], npl + 1.0f);
            return std::clamp((d - npl) / (f - npl), 0.0f, 1.0f);
        };

        renderer.shadow_map.prepare(frame.command_buffer);
        const VkExtent2D shadow_extent{renderer.shadow_map.resolution(),
                                       renderer.shadow_map.resolution()};
        const uint32_t ncas = std::max(1u, renderer.shadow_map.last_cascade_count);
        for (uint32_t c = 0; c < ncas && c < gfx::kShadowCascades; ++c) {
            glm::vec4 sil[gfx::kShadowSilhouettePlanes];
            uint32_t nsil = 0;
            if (have_corners) {
                glm::vec3 slice[8];
                gfx::ShadowMap::slice_frustum_corners(full_corners, 0.0f,
                                                      t_of(edges[c + 1]), slice);
                nsil = gfx::ShadowMap::silhouette_planes(
                    slice, renderer.shadow_map.last_to_light, sil,
                    gfx::kShadowSilhouettePlanes);
            }
            // Whole-mesh casters + silhouette extra planes (Aaltonen receiver cull).
            renderer.gpu_culling.record(
                frame.command_buffer, fi, renderer.shadow_map.last_view_proj[c], false, 0,
                0, 0, 0.003f, gfx::CullEmitFilter::OpaqueDepth, camera.get_position(),
                /*cone_cull=*/false, /*expand_meshlets=*/false, sil, nsil);
            renderer.shadow_map.begin(frame.command_buffer, c);
            record_indirect_draws(frame.command_buffer, renderer, vk.shadow_pipeline,
                                  renderer.shadow_map.last_view_proj[c],
                                  gfx::CullPass::Opaque, shadow_extent);
            renderer.shadow_map.end(frame.command_buffer);
        }
        renderer.shadow_map.finish(frame.command_buffer);

        VkMemoryBarrier inst{};
        inst.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        inst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        inst.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(frame.command_buffer, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &inst, 0,
                             nullptr, 0, nullptr);
    }

    auto record_transparent_cull = [&](bool hzb) {
        if (!gpu_transparent)
            return;
        if (hzb) {
            renderer.gpu_culling.record(frame.command_buffer, fi, viewProj, true,
                                        renderer.hzb.width(), renderer.hzb.height(),
                                        renderer.hzb.mip_count(), 0.003f,
                                        gfx::CullEmitFilter::Transparent,
                                        camera.get_position(), true);
        } else {
            renderer.gpu_culling.record(frame.command_buffer, fi, viewProj, false, 0,
                                        0, 0, 0.003f, gfx::CullEmitFilter::Transparent,
                                        camera.get_position(), true);
        }
    };

    if (can_hzb) {
        {
            auto depth = gpu_times.scope(frame.command_buffer, fi,
                                         core::GpuStage::DepthHzb);
            renderer.gpu_culling.record(frame.command_buffer, fi, viewProj, false, 0, 0,
                                        0, 0.003f, gfx::CullEmitFilter::OpaqueDepth,
                                        camera.get_position(), true);

            VkClearValue clear_depth{};
            clear_depth.depthStencil = {gfx::kDepthClear, 0};

            VkRenderPassBeginInfo prepass_info{};
            prepass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            prepass_info.renderPass = renderer.depth_prepass.render_pass;
            prepass_info.framebuffer = renderer.depth_prepass.framebuffers[fi];
            prepass_info.renderArea.offset = {0, 0};
            prepass_info.renderArea.extent = vk.swap_chain_extent;
            prepass_info.clearValueCount = 1;
            prepass_info.pClearValues = &clear_depth;

            vkCmdBeginRenderPass(frame.command_buffer, &prepass_info,
                                 VK_SUBPASS_CONTENTS_INLINE);
            record_indirect_draws(frame.command_buffer, renderer,
                                  vk.depth_prepass_pipeline, viewProj,
                                  gfx::CullPass::Opaque);
            vkCmdEndRenderPass(frame.command_buffer);

            renderer.hzb.record_build(frame.command_buffer, fi, vk.swap_chain_extent);
        }
        {
            auto cull = gpu_times.scope(frame.command_buffer, fi, core::GpuStage::Cull);
            renderer.gpu_culling.record(frame.command_buffer, fi, viewProj, true,
                                        renderer.hzb.width(), renderer.hzb.height(),
                                        renderer.hzb.mip_count(), 0.003f,
                                        gfx::CullEmitFilter::OpaqueDepth,
                                        camera.get_position(), true);
            record_transparent_cull(true);
        }
        renderer.last_cull_used_hzb[fi] = true;
    } else {
        if (can_cull) {
            auto cull = gpu_times.scope(frame.command_buffer, fi, core::GpuStage::Cull);
            renderer.gpu_culling.record(frame.command_buffer, fi, viewProj, false, 0,
                                        0, 0, 0.003f, gfx::CullEmitFilter::OpaqueDepth,
                                        camera.get_position(), true);
            record_transparent_cull(false);
        }
        renderer.last_cull_used_hzb[fi] = false;
    }

    // 5) Main shade: **opaque first** (depth write on), then **transparent**
    //    (depth test on, depth write off) so glass never blocks the cabin.
    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = renderer.main_pass.render_pass;
    render_pass_info.framebuffer = renderer.main_pass.framebuffers[image_index];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = vk.swap_chain_extent;

    std::array<VkClearValue, 4> clear_values{};
    if (renderer.main_pass.uses_depth_resolve) {
        clear_values[0].color = {{0.02f, 0.02f, 0.03f, 1.0f}}; // MSAA color
        clear_values[1].color = {{0.02f, 0.02f, 0.03f, 1.0f}}; // swapchain (unused)
        clear_values[2].depthStencil = {gfx::kDepthClear, 0};  // MSAA depth
        clear_values[3].depthStencil = {gfx::kDepthClear, 0};  // resolve depth (unused)
        render_pass_info.clearValueCount = 4;
    } else {
        clear_values[0].color = {{0.02f, 0.02f, 0.03f, 1.0f}};
        clear_values[1].depthStencil = {gfx::kDepthClear, 0};
        render_pass_info.clearValueCount = 2;
    }
    render_pass_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(frame.command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    if (can_cull) {
        auto opaque = gpu_times.scope(frame.command_buffer, fi, core::GpuStage::Opaque);
        // Graphics only — no compute/barriers inside the render pass.
        // Opaque + transparent share one instance SSBO (different firstInstance
        // bases); do not re-update binding 1 between these draws.
        record_indirect_draws(frame.command_buffer, renderer, vk.pipeline, viewProj,
                              gfx::CullPass::Opaque);
    }

    if (can_cull && cpu_transparent && vk.transparent_pipeline != VK_NULL_HANDLE) {
        auto trans = gpu_times.scope(frame.command_buffer, fi,
                                     core::GpuStage::Transparent);
        const uint32_t base = renderer.gpu_culling.instance_slot_count();
        renderer.transparent.record_sorted_draws(
            frame.command_buffer, renderer, vk.transparent_pipeline, viewProj,
            base);
    }

    // Physics collider wireframes (after shade so they sit on top with depth test).
    if (physics.is_debug_draw_enabled() && renderer.debug_lines.is_ready()) {
        std::vector<physics::DebugVertex> lines;
        physics.collect_debug_lines(lines, camera.get_position());
        if (!lines.empty()) {
            renderer.debug_lines.draw(frame.command_buffer, fi,
                                      vk.swap_chain_extent, viewProj, lines);
        }
    }

    vkCmdEndRenderPass(frame.command_buffer);

    bool presented_by_wboit = false;
    if (gpu_transparent ||
        (renderer.transparent.wboit_ready() && renderer.transparent.count() > 0)) {
        auto trans = gpu_times.scope(frame.command_buffer, fi,
                                     core::GpuStage::Transparent);
        renderer.transparent.record_wboit(frame.command_buffer, renderer, fi,
                                          image_index, viewProj, gpu_transparent);
        presented_by_wboit = true;
    }

    const bool draw_hud = renderer.hud_text.is_ready() && renderer.hud_text.has_text() &&
                          image_index < vk.swap_chain_images.size();
    if (draw_hud) {
        auto overlay = gpu_times.scope(frame.command_buffer, fi,
                                       core::GpuStage::Overlay);
        if (presented_by_wboit) {
            VkImageMemoryBarrier to_color{};
            to_color.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            to_color.srcAccessMask = 0;
            to_color.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                     VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            to_color.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            to_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            to_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            to_color.image = vk.swap_chain_images[image_index];
            to_color.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(frame.command_buffer,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &to_color);
        }
        renderer.hud_text.draw(frame.command_buffer, fi, image_index,
                               vk.swap_chain_extent, proj);
    } else if (!presented_by_wboit && image_index < vk.swap_chain_images.size()) {
        VkImageMemoryBarrier to_present{};
        to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_present.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        to_present.dstAccessMask = 0;
        to_present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.image = vk.swap_chain_images[image_index];
        to_present.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(frame.command_buffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &to_present);
    }

    gpu_times.tracy_collect(frame.command_buffer);
    if (vkEndCommandBuffer(frame.command_buffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to end command buffer");
        return;
    }
    } // CpuStage::Record

    {
        auto present = frame_stats.scope(core::CpuStage::Present);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore wait_semaphores[] = {vk.image_available_semaphores[renderer.current_frame]};
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &frame.command_buffer;

        VkSemaphore signal_semaphores[] = {vk.render_finished_semaphores[image_index]};
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        result = vkQueueSubmit(vk.graphics_queue, 1, &submit_info, frame.in_flight_fence);
        if (result != VK_SUCCESS) {
            if (result == VK_ERROR_DEVICE_LOST) {
                mark_device_lost("vkQueueSubmit");
            } else {
                LOG_ERROR("vkQueueSubmit failed with VkResult=" << (int)result);
            }
            return;
        }
        gpu_times.mark_submitted(fi);

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;

        VkSwapchainKHR swapchains[] = {vk.swapchain};
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swapchains;
        present_info.pImageIndices = &image_index;

        result = vkQueuePresentKHR(vk.present_queue, &present_info);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            recreate_swapchain();
        } else if (result == VK_ERROR_SURFACE_LOST_KHR) {
            mark_device_lost("vkQueuePresentKHR SURFACE_LOST");
        } else if (result == VK_ERROR_DEVICE_LOST) {
            mark_device_lost("vkQueuePresentKHR");
        } else if (result != VK_SUCCESS) {
            LOG_ERROR("vkQueuePresentKHR failed with VkResult=" << (int)result);
        }
    }

    renderer.current_frame = (renderer.current_frame + 1) % Renderer::MAX_FRAMES_IN_FLIGHT;
}
