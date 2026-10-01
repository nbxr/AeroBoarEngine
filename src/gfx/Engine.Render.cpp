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
#include "core/Configuration.h"
#include "core/AABB.h"
#include "gfx/Material.h"
#include "gfx/ShadowCasterVolume.h"
#include <cmath>
#include <string>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

bool aabb_outside_plane(const core::AABB& b, const glm::vec4& pl) {
    const glm::vec3 n(pl);
    const glm::vec3 p(n.x >= 0.0f ? b.max.x : b.min.x, n.y >= 0.0f ? b.max.y : b.min.y,
                      n.z >= 0.0f ? b.max.z : b.min.z);
    return glm::dot(n, p) + pl.w < -0.02f;
}

bool aabb_in_planes(const core::AABB& b, const glm::vec4* planes, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        if (aabb_outside_plane(b, planes[i]))
            return false;
    }
    return true;
}

bool point_in_vp(const glm::vec3& p, const glm::mat4& vp) {
    const core::Frustum fr = core::Frustum::from_view_proj(vp);
    for (int i = 0; i < 6; ++i) {
        if (glm::dot(glm::vec3(fr.planes[i]), p) + fr.planes[i].w < -0.02f)
            return false;
    }
    return true;
}

bool ndc_on_map(const glm::vec3& p, const glm::mat4& view_proj) {
    glm::vec4 c = view_proj * glm::vec4(p, 1.0f);
    if (std::abs(c.w) < 1e-8f)
        return false;
    c /= c.w;
    const glm::vec2 uv = glm::vec2(c) * 0.5f + 0.5f;
    return uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f && c.z >= 0.0f &&
           c.z <= 1.0f;
}

bool aabb_hits_map(const core::AABB& b, const glm::mat4& view_proj) {
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p((i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y,
                          (i & 4) ? b.max.z : b.min.z);
        if (ndc_on_map(p, view_proj))
            return true;
    }
    return false;
}

int shade_cascade(float vz, const glm::vec4& splits) {
    const uint32_t n = static_cast<uint32_t>(splits.w + 0.5f);
    if (n < 2u)
        return 0;
    if (vz < splits.x)
        return 0;
    if (vz < splits.y)
        return 1;
    return 2;
}

// One line per Pawn_Body_*: contact (feet) and the head-projected board point,
// plus a point one body-length past that. Tags the shade cascade only.
void log_pawn_census(scene::Camera& camera, gfx::Renderer& renderer) {
    auto& sm = renderer.shadow_map;
    if (!sm.last_on)
        return;
    const glm::vec3 cam_p = camera.get_position();
    const glm::vec3 cam_f = camera.get_forward();
    LOG_INFO("[ShadowProbe] census cam pos=(" << cam_p.x << "," << cam_p.y << ","
             << cam_p.z << ") fwd=(" << cam_f.x << "," << cam_f.y << "," << cam_f.z
             << ") splits=(" << sm.last_splits.x << "," << sm.last_splits.y << ","
             << sm.last_splits.z << ")");
    const glm::vec3 L = -glm::normalize(sm.last_to_light);
    const auto& xforms = renderer.scene_manager.transforms();
    const uint32_t skip =
        gfx::Material::kFlagAlphaBlend | gfx::Material::kFlagTransmission;
    float aspect = 16.0f / 9.0f;
    if (renderer.vk.swap_chain_extent.height > 0)
        aspect = (float)renderer.vk.swap_chain_extent.width /
                 (float)renderer.vk.swap_chain_extent.height;
    glm::mat4 proj = camera.get_projection_matrix(aspect);
    proj[1][1] *= -1.0f;
    const glm::mat4 cam_vp = proj * camera.get_view_matrix();

    auto unproj = [](const glm::mat4& inv, float x, float y, float z) {
        glm::vec4 c = inv * glm::vec4(x, y, z, 1.0f);
        return glm::vec3(c) / std::max(std::abs(c.w), 1e-8f);
    };
    const float res = static_cast<float>(std::max(1u, sm.resolution()));
    for (uint32_t c = 0; c < sm.last_cascade_count && c < gfx::kShadowCascades; ++c) {
        const glm::mat4 inv = glm::inverse(sm.last_view_proj[c]);
        const float xw = glm::length(unproj(inv, 1.0f, 0.0f, 0.5f) -
                                     unproj(inv, -1.0f, 0.0f, 0.5f));
        const float yw = glm::length(unproj(inv, 0.0f, 1.0f, 0.5f) -
                                     unproj(inv, 0.0f, -1.0f, 0.5f));
        const float zw = glm::length(unproj(inv, 0.0f, 0.0f, 1.0f) -
                                     unproj(inv, 0.0f, 0.0f, 0.0f));
        LOG_INFO("[ShadowProbe] cas" << c << " xy=(" << xw << "," << yw
                 << ") zspan=" << zw << " texel=" << (xw / res)
                 << " ndcBias0.002m=" << (0.002f * zw));
    }

    for (uint32_t i = 0; i < renderer.scene_manager.game_object_count(); ++i) {
        const auto& g = renderer.scene_manager.get_game_object(i);
        if (g.name.rfind("Pawn_Body_", 0) != 0 || g.render_mesh_count == 0)
            continue;
        core::AABB box{};
        for (uint32_t k = 0; k < g.render_mesh_count; ++k) {
            const auto& rm =
                renderer.scene_manager.get_render_mesh(g.first_render_mesh + k);
            const uint32_t fl =
                renderer.material_manager.get_material_flags(rm.material_index);
            if ((fl & skip) != 0u || !rm.local_aabb.is_valid())
                continue;
            const core::AABB wa =
                rm.local_aabb.transformed(xforms.get_world_matrix(rm.transform_index));
            if (wa.is_valid())
                box.expand(wa);
        }
        if (!box.is_valid())
            continue;
        const glm::vec3 feet(box.center().x, box.min.y, box.center().z);
        glm::vec3 far = glm::vec3(box.center().x, box.max.y, box.center().z);
        if (L.y < -1e-4f)
            far = far + L * ((box.min.y - far.y) / L.y);
        const glm::vec3 past = far + (far - feet);
        const glm::vec3 samples[3] = {feet, far, past};
        const char* sname[3] = {"feet", "far", "past"};
        std::string line = "[ShadowProbe] " + g.name;
        bool any_bad = false;
        for (int s = 0; s < 3; ++s) {
            const glm::vec3& Rp = samples[s];
            const float vz = glm::dot(Rp - cam_p, cam_f);
            const int si = shade_cascade(vz, sm.last_splits);
            glm::vec4 clip = cam_vp * glm::vec4(Rp, 1.0f);
            float su = 2.0f, sv = 2.0f;
            if (std::abs(clip.w) > 1e-8f) {
                clip /= clip.w;
                su = clip.x * 0.5f + 0.5f;
                sv = clip.y * 0.5f + 0.5f;
            }
            const bool on_screen = su >= 0.0f && su <= 1.0f && sv >= 0.0f && sv <= 1.0f;
            if (!on_screen)
                continue;
            float blend_t = 0.0f;
            if (si == 0 || si == 1) {
                const float hi = (si == 0) ? sm.last_splits.x : sm.last_splits.y;
                const float band = std::max(hi * 0.18f, 0.35f);
                blend_t = std::clamp((vz - (hi - band)) / std::max(band, 1e-3f), 0.0f, 1.0f);
            }
            float lz = -1.0f;
            float smu = -1.0f;
            float smv = -1.0f;
            if (si >= 0 && si < (int)gfx::kShadowCascades) {
                glm::vec4 lc = sm.last_view_proj[si] * glm::vec4(Rp, 1.0f);
                if (std::abs(lc.w) > 1e-8f) {
                    lc /= lc.w;
                    lz = lc.z;
                    smu = lc.x * 0.5f + 0.5f;
                    smv = lc.y * 0.5f + 0.5f;
                }
            }
            int volm = 0;
            for (uint32_t c = 0; c < sm.last_cascade_count && c < gfx::kShadowCascades;
                 ++c) {
                if (aabb_in_planes(box, sm.last_caster[c].planes, sm.last_caster[c].count))
                    volm |= 1 << c;
            }
            const glm::vec3 head(box.center().x, box.max.y, box.center().z);
            float head_lz = -1.0f;
            if (si >= 0 && si < (int)gfx::kShadowCascades) {
                glm::vec4 hc = sm.last_view_proj[si] * glm::vec4(head, 1.0f);
                if (std::abs(hc.w) > 1e-8f)
                    head_lz = (hc / hc.w).z;
            }
            float c2 = 1.0e9f;
            int c2p = -1;
            if (sm.last_caster[2].count > 0) {
                for (uint32_t p = 0; p < sm.last_caster[2].count; ++p) {
                    const glm::vec4& pl = sm.last_caster[2].planes[p];
                    const glm::vec3 n(pl);
                    const glm::vec3 pv(n.x >= 0.0f ? box.max.x : box.min.x,
                                       n.y >= 0.0f ? box.max.y : box.min.y,
                                       n.z >= 0.0f ? box.max.z : box.min.z);
                    const float d = glm::dot(n, pv) + pl.w;
                    if (d < c2) {
                        c2 = d;
                        c2p = (int)p;
                    }
                }
            }
            char extra[256];
            std::snprintf(extra, sizeof(extra),
                          " %s uv=%.2f,%.2f vz=%.3f si=%d blend=%.2f lz=%.3f "
                          "headLz=%.3f vol=%d c2=%.3f/%d suv=%.4f,%.4f",
                          sname[s], su, sv, vz, si, blend_t, lz, head_lz, volm, c2,
                          c2p, smu, smv);
            line += extra;
            if (si >= 0 && si < (int)gfx::kShadowCascades) {
                const bool pyr = point_in_vp(Rp, sm.last_receiver_vp[si]);
                const bool vol = aabb_in_planes(box, sm.last_caster[si].planes,
                                                sm.last_caster[si].count);
                const bool mapR = ndc_on_map(Rp, sm.last_view_proj[si]);
                const bool mapC = aabb_hits_map(box, sm.last_view_proj[si]);
                line += " pyr=" + std::to_string(pyr ? 1 : 0) +
                        " vol=" + std::to_string(vol ? 1 : 0) +
                        " mapC=" + std::to_string(mapC ? 1 : 0) +
                        " mapR=" + std::to_string(mapR ? 1 : 0);
                if (on_screen && !pyr) {
                    const core::Frustum fr =
                        core::Frustum::from_view_proj(sm.last_receiver_vp[si]);
                    float worst = 1.0e9f;
                    int wi = -1;
                    for (int p = 0; p < 6; ++p) {
                        const float d =
                            glm::dot(glm::vec3(fr.planes[p]), Rp) + fr.planes[p].w;
                        if (d < worst) {
                            worst = d;
                            wi = p;
                        }
                    }
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), " plane%d=%.3f", wi, worst);
                    line += buf;
                    int pyrs = 0;
                    for (uint32_t c = 0; c < sm.last_cascade_count &&
                                         c < gfx::kShadowCascades;
                         ++c) {
                        if (point_in_vp(Rp, sm.last_receiver_vp[c]))
                            pyrs |= 1 << c;
                    }
                    line += " pyrMask=" + std::to_string(pyrs);
                }
                if (on_screen && (!pyr || (pyr && !vol) || (vol && !mapR)))
                    any_bad = true;
            }
        }
        if (line.size() <= g.name.size() + 14)
            continue;
        if (any_bad)
            line += " BAD";
        LOG_INFO(line);
    }
}

void log_queen_shadow_probe(scene::Camera& camera, gfx::Renderer& renderer) {
    auto& sm = renderer.shadow_map;
    if (!sm.last_on)
        return;
    const auto& xforms = renderer.scene_manager.transforms();
    core::AABB queen{};
    const scene::GameObject* go = nullptr;
    for (uint32_t i = 0; i < renderer.scene_manager.game_object_count(); ++i) {
        const auto& g = renderer.scene_manager.get_game_object(i);
        if (g.name != core::Configuration::get_instance().debug.shadow_probe_target)
            continue;
        go = &g;
        const uint32_t skip =
            gfx::Material::kFlagAlphaBlend | gfx::Material::kFlagTransmission;
        for (uint32_t k = 0; k < g.render_mesh_count; ++k) {
            const auto& rm =
                renderer.scene_manager.get_render_mesh(g.first_render_mesh + k);
            const uint32_t fl =
                renderer.material_manager.get_material_flags(rm.material_index);
            if ((fl & skip) != 0u || !rm.local_aabb.is_valid())
                continue;
            const glm::mat4& w = xforms.get_world_matrix(rm.transform_index);
            const core::AABB wa = rm.local_aabb.transformed(w);
            if (wa.is_valid())
                queen.expand(wa);
        }
        break;
    }
    if (!go || !queen.is_valid()) {
        LOG_INFO("[ShadowProbe] " << core::Configuration::get_instance().debug.shadow_probe_target
                 << " not found or empty AABB");
        return;
    }
    const glm::vec3 C = queen.center();
    const glm::vec3 head(C.x, queen.max.y, C.z);
    glm::vec3 Lto = glm::normalize(sm.last_to_light);
    const glm::vec3 L = -Lto;
    const glm::vec3 cam_p = camera.get_position();
    const glm::vec3 cam_f = camera.get_forward();
    glm::vec3 R = head;
    if (L.y < -1e-4f) {
        const float t = (queen.min.y - head.y) / L.y;
        R = head + L * t;
    }
    LOG_INFO("[ShadowProbe] Queen_B C=(" << C.x << "," << C.y << "," << C.z
             << ") head=(" << head.x << "," << head.y << "," << head.z
             << ") aabb=(" << queen.min.x << ".." << queen.max.x << ","
             << queen.min.y << ".." << queen.max.y << "," << queen.min.z << ".."
             << queen.max.z << ")");
    LOG_INFO("[ShadowProbe] L_shadow=(" << L.x << "," << L.y << "," << L.z
             << ") splits=(" << sm.last_splits.x << "," << sm.last_splits.y
             << "," << sm.last_splits.z << ") ncas="
             << sm.last_cascade_count);
    {
        const glm::mat4& vp = sm.last_view_proj[1];
        std::ostringstream uvss;
        uvss << "[ShadowProbe] cas1 AABB-corner uv:";
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 p((i & 1) ? queen.max.x : queen.min.x,
                              (i & 2) ? queen.max.y : queen.min.y,
                              (i & 4) ? queen.max.z : queen.min.z);
            glm::vec4 c = vp * glm::vec4(p, 1.0f);
            if (std::abs(c.w) < 1e-8f)
                continue;
            c /= c.w;
            uvss << " (" << (c.x * 0.5f + 0.5f) << "," << (c.y * 0.5f + 0.5f)
                 << ",z=" << c.z << ")";
        }
        LOG_INFO(uvss.str());
    }
    {
        float aspect = 16.0f / 9.0f;
        if (renderer.vk.swap_chain_extent.height > 0)
            aspect = (float)renderer.vk.swap_chain_extent.width /
                     (float)renderer.vk.swap_chain_extent.height;
        glm::mat4 proj = camera.get_projection_matrix(aspect);
        proj[1][1] *= -1.0f;
        glm::vec4 c = (proj * camera.get_view_matrix()) * glm::vec4(R, 1.0f);
        if (std::abs(c.w) > 1e-8f) {
            c /= c.w;
            LOG_INFO("[ShadowProbe] R_board=(" << R.x << "," << R.y << "," << R.z
                     << ") shade_uv=(" << (c.x * 0.5f + 0.5f) << ","
                     << (c.y * 0.5f + 0.5f) << ")");
        }
    }

    glm::vec3 Rs[2] = {head, R};
    const char* rname[2] = {"head", "board"};

    for (uint32_t ri = 0; ri < 2; ++ri) {
        const glm::vec3& Rp = Rs[ri];
        const float vz = glm::dot(Rp - cam_p, cam_f);
        const int si = shade_cascade(vz, sm.last_splits);
        LOG_INFO("[ShadowProbe] R." << rname[ri] << "=(" << Rp.x << "," << Rp.y
                 << "," << Rp.z << ") vz=" << vz << " shade_i=" << si);
        const uint32_t ncas = std::max(1u, sm.last_cascade_count);
        for (uint32_t i = 0; i < ncas && i < gfx::kShadowCascades; ++i) {
            const bool pyr = point_in_vp(Rp, sm.last_receiver_vp[i]);
            const bool vol = aabb_in_planes(queen, sm.last_caster[i].planes,
                                            sm.last_caster[i].count);
            const bool mapC = aabb_hits_map(queen, sm.last_view_proj[i]);
            const bool mapR = ndc_on_map(Rp, sm.last_view_proj[i]);
            const char* product = "ok";
            if (si == static_cast<int>(i) && !pyr)
                product = "P1 sampling!=pyramid";
            else if (pyr && !vol)
                product = "P2 volume excludes C";
            else if (vol && (!mapC || !mapR))
                product = "P3 ortho miss";
            glm::vec4 hc = sm.last_view_proj[i] * glm::vec4(head, 1.0f);
            float hu = 2.0f, hv = 2.0f, hz = 2.0f;
            if (std::abs(hc.w) > 1e-8f) {
                hc /= hc.w;
                hu = hc.x * 0.5f + 0.5f;
                hv = hc.y * 0.5f + 0.5f;
                hz = hc.z;
            }
            LOG_INFO("[ShadowProbe]   cas" << i << " pyr=" << (pyr ? 1 : 0)
                     << " vol=" << (vol ? 1 : 0) << " mapC=" << (mapC ? 1 : 0)
                     << " mapR=" << (mapR ? 1 : 0) << " planes="
                     << sm.last_caster[i].count << " head_uvz=(" << hu << ","
                     << hv << "," << hz << ")"
                     << (hz < 0.0f || hz > 1.0f ? " ZCLIP" : "")
                     << " -> " << product);
        }
    }

    // GPU cull is per render-mesh AABB, extra_planes only (skip ortho). Union AABB
    // can pass while the head mesh fails. Match cull_frustum.comp pad / -0.02.
    const gfx::CasterVolume& cas1 = sm.last_caster[1];
    const uint32_t skip =
        gfx::Material::kFlagAlphaBlend | gfx::Material::kFlagTransmission;
    for (uint32_t k = 0; k < go->render_mesh_count; ++k) {
        const auto& rm =
            renderer.scene_manager.get_render_mesh(go->first_render_mesh + k);
        const uint32_t fl =
            renderer.material_manager.get_material_flags(rm.material_index);
        const glm::mat4& w = xforms.get_world_matrix(rm.transform_index);
        const core::AABB wa = rm.local_aabb.transformed(w);
        bool emit = ((fl & skip) == 0u) && wa.is_valid();
        float worst = 1.0e9f;
        if (emit && cas1.count > 0) {
            glm::vec3 wmin = wa.min;
            glm::vec3 wmax = wa.max;
            const glm::vec3 ext = wmax - wmin;
            const glm::vec3 pad = glm::max(ext * 0.01f, glm::vec3(1e-4f));
            wmin -= pad;
            wmax += pad;
            for (uint32_t p = 0; p < cas1.count; ++p) {
                const glm::vec3 n(cas1.planes[p]);
                const glm::vec3 pv(n.x >= 0.0f ? wmax.x : wmin.x,
                                   n.y >= 0.0f ? wmax.y : wmin.y,
                                   n.z >= 0.0f ? wmax.z : wmin.z);
                const float d = glm::dot(n, pv) + cas1.planes[p].w;
                worst = std::min(worst, d);
                if (d < -0.02f)
                    emit = false;
            }
        }
        LOG_INFO("[ShadowProbe] cas1 mesh" << k << " y=" << wa.min.y << ".."
                 << wa.max.y << " flags=" << fl << " extra_emit=" << (emit ? 1 : 0)
                 << " extra_worst=" << worst);
        const core::Frustum ofr =
            core::Frustum::from_view_proj(sm.last_view_proj[1]);
        float fworst = 1.0e9f;
        bool fpass = wa.is_valid();
        if (fpass) {
            glm::vec3 wmin = wa.min, wmax = wa.max;
            const glm::vec3 ext = wmax - wmin;
            const glm::vec3 pad = glm::max(ext * 0.01f, glm::vec3(1e-4f));
            wmin -= pad;
            wmax += pad;
            for (int p = 0; p < 6; ++p) {
                const glm::vec3 n(ofr.planes[p]);
                const glm::vec3 pv(n.x >= 0.0f ? wmax.x : wmin.x,
                                   n.y >= 0.0f ? wmax.y : wmin.y,
                                   n.z >= 0.0f ? wmax.z : wmin.z);
                const float d = glm::dot(n, pv) + ofr.planes[p].w;
                fworst = std::min(fworst, d);
                if (d < -0.02f)
                    fpass = false;
            }
        }
        LOG_INFO("[ShadowProbe] cas1 mesh" << k << " ortho6_pass=" << (fpass ? 1 : 0)
                 << " ortho6_worst=" << fworst
                 << " (if extra_emit=1 and ortho6_pass=0, GPU skip_frustum must be 1)");
    }
}

int g_inst_slot = -1;
glm::vec3 g_queen_t{0.0f};

int g_dump_layer = 1;
int g_cas1_texel_slot = -1;
uint32_t g_cas1_texel_x = 0;
uint32_t g_cas1_texel_y = 0;
uint32_t g_cas1_feet_x = 0;
uint32_t g_cas1_feet_y = 0;
float g_cas1_head_z = 0.0f;
float g_cas1_board_z = 0.0f;
float g_cas1_feet_z = 0.0f;
glm::mat4 g_cas1_vp{1.0f};
glm::vec3 g_cas1_head_w{0.0f};
core::AABB g_cas1_queen{};

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

    auto& vertex_buf = renderer.mesh_manager.get_render_vertex_buffer();
    VkDeviceSize vbo_offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buf.buffer, &vbo_offset);

    gfx::PbrPush pushData{};
    pushData.viewProj = viewProj;
    pushData.extra = glm::uvec4{0u, 0u, 0u, 0u};

    vkCmdPushConstants(cmd, vk.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(gfx::PbrPush), &pushData);

    auto& ib16 = renderer.mesh_manager.get_render_index16_buffer();
    if (ib16.buffer != VK_NULL_HANDLE) {
        vkCmdBindIndexBuffer(cmd, ib16.buffer, 0, VK_INDEX_TYPE_UINT16);
        renderer.gpu_culling.cmd_draw_indexed(cmd, renderer.current_frame, pass, true);
    }
    auto& ib32 = renderer.mesh_manager.get_render_index_buffer();
    if (ib32.buffer != VK_NULL_HANDLE) {
        vkCmdBindIndexBuffer(cmd, ib32.buffer, 0, VK_INDEX_TYPE_UINT32);
        renderer.gpu_culling.cmd_draw_indexed(cmd, renderer.current_frame, pass, false);
    }
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

        if (g_inst_slot == static_cast<int>(renderer.current_frame)) {
            const uint32_t n =
                renderer.gpu_culling.count_copied_models_near(g_queen_t, 0.05f);
            LOG_INFO("[ShadowProbe] cas1 GPU instances near Queen t=("
                     << g_queen_t.x << "," << g_queen_t.y << "," << g_queen_t.z
                     << ") count=" << n);
            g_inst_slot = -2;
        }

        if (g_cas1_texel_slot == static_cast<int>(renderer.current_frame)) {
            const std::string& dump =
                core::Configuration::get_instance().debug.shadow_map_dump;
            const float hi_lo = 0.5f * (g_cas1_head_z + g_cas1_board_z);
            if (dump == "layer") {
                float mn = 0.0f, mx = 0.0f;
                uint32_t n_hi = 0, ax = 0, ay = 0;
                const uint32_t n = renderer.shadow_map.resolution();
                renderer.shadow_map.read_patch_minmax(n, mn, mx, n_hi, hi_lo, &ax, &ay);
                const float u = (static_cast<float>(ax) + 0.5f) / static_cast<float>(n);
                const float v = (static_cast<float>(ay) + 0.5f) / static_cast<float>(n);
                glm::vec4 world = glm::inverse(g_cas1_vp) *
                                  glm::vec4(u * 2.0f - 1.0f, v * 2.0f - 1.0f, mx, 1.0f);
                if (std::abs(world.w) > 1e-8f)
                    world /= world.w;
                const float dhead = glm::length(glm::vec3(world) - g_cas1_head_w);
                LOG_INFO("[ShadowProbe] cas1 LAYER min=" << mn << " max=" << mx
                         << " max_px=(" << ax << "," << ay << ") head_px=("
                         << g_cas1_texel_x << "," << g_cas1_texel_y << ") n_crown="
                         << n_hi << "/" << (n * n) << " head_z=" << g_cas1_head_z
                         << " board_z=" << g_cas1_board_z);
                LOG_INFO("[ShadowProbe] cas1 max world=(" << world.x << "," << world.y
                         << "," << world.z << ") dist_head=" << dhead);
                const float* depth = renderer.shadow_map.texel_readback_data();
                if (depth && g_cas1_queen.is_valid()) {
                    const glm::mat4 inv = glm::inverse(g_cas1_vp);
                    uint32_t nq = 0, qax = 0, qay = 0;
                    float qmax = -1.0f;
                    for (uint32_t i = 0; i < n * n; ++i) {
                        const float z = depth[i];
                        if (z < hi_lo)
                            continue;
                        const uint32_t x = i % n;
                        const uint32_t y = i / n;
                        const float uu = (x + 0.5f) / (float)n;
                        const float vv = (y + 0.5f) / (float)n;
                        glm::vec4 wld = inv * glm::vec4(uu * 2.0f - 1.0f, vv * 2.0f - 1.0f, z, 1.0f);
                        if (std::abs(wld.w) > 1e-8f)
                            wld /= wld.w;
                        const glm::vec3 p(wld);
                        if (p.x < g_cas1_queen.min.x - 0.05f || p.x > g_cas1_queen.max.x + 0.05f ||
                            p.y < g_cas1_queen.min.y - 0.05f || p.y > g_cas1_queen.max.y + 0.05f ||
                            p.z < g_cas1_queen.min.z - 0.05f || p.z > g_cas1_queen.max.z + 0.05f)
                            continue;
                        ++nq;
                        if (z > qmax) {
                            qmax = z;
                            qax = x;
                            qay = y;
                        }
                    }
                    LOG_INFO("[ShadowProbe] cas1 in-queen hi texels=" << nq
                             << " queen_max=" << qmax << " queen_max_px=(" << qax
                             << "," << qay << ")");
                }
            } else if (dump == "patch17") {
                float mn = 0.0f, mx = 0.0f;
                uint32_t n_hi = 0;
                renderer.shadow_map.read_patch_minmax(17, mn, mx, n_hi, hi_lo);
                LOG_INFO("[ShadowProbe] cas1 HEAD 17x17 px=(" << g_cas1_texel_x << ","
                         << g_cas1_texel_y << ") min=" << mn << " max=" << mx
                         << " n_crown=" << n_hi << "/289 head_z=" << g_cas1_head_z);
            } else if (dump == "texel") {
                float h[9];
                renderer.shadow_map.read_copied_3x3(h, 0);
                LOG_INFO("[ShadowProbe] cas" << g_dump_layer << " BOARD 3x3 px=("
                         << g_cas1_texel_x << "," << g_cas1_texel_y
                         << ") head_z=" << g_cas1_head_z
                         << " board_z=" << g_cas1_board_z);
                LOG_INFO("[ShadowProbe]   " << h[0] << " " << h[1] << " " << h[2]);
                LOG_INFO("[ShadowProbe]   " << h[3] << " " << h[4] << " " << h[5]);
                LOG_INFO("[ShadowProbe]   " << h[6] << " " << h[7] << " " << h[8]);
            }
            g_cas1_texel_slot = -2;
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
                        LOG_CULL("[Cull] " << culled << " of " << total
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
                            LOG_CULL("[Meshlet] " << ml_culled << " of " << ml.tested
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
        if (core::Configuration::get_instance().debug.queen_shadow_probe) {
            static int s_probe_frame = 0;
            ++s_probe_frame;
            // Locked pose: first lit frame. Free boom: wait until the follow
            // camera has been written (default third-person view).
            const bool go = camera.pose_locked ? (s_probe_frame == 1)
                                               : (s_probe_frame == 8);
            if (go) {
                const auto& tgt =
                    core::Configuration::get_instance().debug.shadow_probe_target;
                if (tgt == "pawns")
                    log_pawn_census(camera, renderer);
                else
                    log_queen_shadow_probe(camera, renderer);
            }
        }
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
        renderer.shadow_map.prepare(frame.command_buffer);
        const VkExtent2D shadow_extent{renderer.shadow_map.resolution(),
                                       renderer.shadow_map.resolution()};
        const uint32_t ncas = std::max(1u, renderer.shadow_map.last_cascade_count);
        for (uint32_t c = 0; c < ncas && c < gfx::kShadowCascades; ++c) {
            const gfx::CasterVolume& vol = renderer.shadow_map.last_caster[c];
            const glm::vec4* extra = nullptr;
            uint32_t nextra = 0;
            bool skip_ortho = false;
            if (renderer.shadow_map.silhouette) {
                extra = vol.planes;
                nextra = vol.count;
                // Caster volume is extra_planes. Skip the 6 ortho planes (those
                // clip the map, not the volume). extra_count==0 → emit all.
                skip_ortho = true;
            }
            renderer.gpu_culling.record(
                frame.command_buffer, fi, renderer.shadow_map.last_view_proj[c], false, 0,
                0, 0, 0.003f, gfx::CullEmitFilter::OpaqueDepth, camera.get_position(),
                /*cone_cull=*/false, /*expand_meshlets=*/false, extra, nextra,
                skip_ortho);
            if (c == 1u && camera.pose_locked && g_inst_slot == -1 &&
                core::Configuration::get_instance().debug.queen_shadow_probe) {
                const auto& dbg = core::Configuration::get_instance().debug;
                for (uint32_t i = 0; i < renderer.scene_manager.game_object_count(); ++i) {
                    const auto& g = renderer.scene_manager.get_game_object(i);
                    if (g.name != dbg.shadow_probe_target || g.render_mesh_count == 0)
                        continue;
                    const auto& rm =
                        renderer.scene_manager.get_render_mesh(g.first_render_mesh);
                    g_queen_t = glm::vec3(
                        renderer.scene_manager.transforms().get_world_matrix(
                            rm.transform_index)[3]);
                    uint32_t batch = 0, base = 0, cap = 0;
                    const bool in_items = renderer.gpu_culling.lookup_transform(
                        rm.transform_index, batch, base, cap);
                    const glm::vec3 wt = renderer.gpu_culling.world_translation(
                        fi, rm.transform_index);
                    LOG_INFO("[ShadowProbe] cas1 item ti=" << rm.transform_index
                             << " in_cpu_items=" << (in_items ? 1 : 0)
                             << " batch=" << batch << " base=" << base
                             << " cap=" << cap << " cpu_t=(" << g_queen_t.x << ","
                             << g_queen_t.y << "," << g_queen_t.z << ") ssbo_t=("
                             << wt.x << "," << wt.y << "," << wt.z << ")");
                    break;
                }
                renderer.gpu_culling.copy_opaque_instances(frame.command_buffer, fi);
                g_inst_slot = static_cast<int>(fi);
            }
            renderer.shadow_map.begin(frame.command_buffer, c);
            record_indirect_draws(frame.command_buffer, renderer, vk.shadow_pipeline,
                                  renderer.shadow_map.last_view_proj[c],
                                  gfx::CullPass::Opaque, shadow_extent);
            renderer.shadow_map.end(frame.command_buffer);
        }
        renderer.shadow_map.finish(frame.command_buffer);
        {
            const auto& dbg = core::Configuration::get_instance().debug;
            const std::string& dump = dbg.shadow_map_dump;
            if (camera.pose_locked && g_cas1_texel_slot == -1 && dump != "off" &&
                !dump.empty()) {
                glm::vec3 head(0.0f);
                glm::vec3 board(0.0f);
                core::AABB queen_go{};
                bool have = false;
                for (uint32_t i = 0; i < renderer.scene_manager.game_object_count(); ++i) {
                    const auto& g = renderer.scene_manager.get_game_object(i);
                    if (g.name != dbg.shadow_probe_target)
                        continue;
                    core::AABB queen{};
                    const uint32_t skip = gfx::Material::kFlagAlphaBlend |
                                          gfx::Material::kFlagTransmission;
                    for (uint32_t k = 0; k < g.render_mesh_count; ++k) {
                        const auto& rm =
                            renderer.scene_manager.get_render_mesh(g.first_render_mesh + k);
                        const uint32_t fl =
                            renderer.material_manager.get_material_flags(rm.material_index);
                        if ((fl & skip) != 0u || !rm.local_aabb.is_valid())
                            continue;
                        const glm::mat4& w =
                            renderer.scene_manager.transforms().get_world_matrix(
                                rm.transform_index);
                        const core::AABB wa = rm.local_aabb.transformed(w);
                        if (wa.is_valid())
                            queen.expand(wa);
                    }
                    if (!queen.is_valid())
                        break;
                    head = glm::vec3(queen.center().x, queen.max.y, queen.center().z);
                    const glm::vec3 L = -glm::normalize(renderer.shadow_map.last_to_light);
                    board = head;
                    if (L.y < -1e-4f)
                        board = head + L * ((queen.min.y - head.y) / L.y);
                    have = true;
                    queen_go = queen;
                    break;
                }
                if (have) {
                    auto uvz = [](const glm::vec3& p, const glm::mat4& vp) {
                        glm::vec4 c = vp * glm::vec4(p, 1.0f);
                        if (std::abs(c.w) < 1e-8f)
                            return glm::vec3(0.0f);
                        c /= c.w;
                        return glm::vec3(c.x * 0.5f + 0.5f, c.y * 0.5f + 0.5f, c.z);
                    };
                    const float head_vz =
                        glm::dot(head - camera.get_position(), camera.get_forward());
                    int layer = shade_cascade(head_vz, renderer.shadow_map.last_splits);
                    if (layer < 0)
                        layer = 0;
                    if (layer >= (int)gfx::kShadowCascades)
                        layer = (int)gfx::kShadowCascades - 1;
                    g_dump_layer = layer;
                    const glm::mat4& vp = renderer.shadow_map.last_view_proj[layer];
                    const glm::vec3 hu = uvz(head, vp);
                    const glm::vec3 bu = uvz(board, vp);
                    const uint32_t res = renderer.shadow_map.resolution();
                    auto px = [res](float u) {
                        const int i = (int)std::floor(u * (float)res);
                        if (i < 0)
                            return 0u;
                        if (i >= (int)res)
                            return res - 1u;
                        return (uint32_t)i;
                    };
                    g_cas1_texel_x = px(bu.x);
                    g_cas1_texel_y = px(bu.y);
                    g_cas1_head_z = hu.z;
                    g_cas1_board_z = bu.z;
                    g_cas1_vp = vp;
                    g_cas1_head_w = head;
                    g_cas1_queen = queen_go;
                    if (dump == "layer")
                        renderer.shadow_map.copy_layer(frame.command_buffer,
                                                       (uint32_t)layer);
                    else if (dump == "patch17")
                        renderer.shadow_map.copy_patch(frame.command_buffer,
                                                       (uint32_t)layer, g_cas1_texel_x,
                                                       g_cas1_texel_y, 17);
                    else if (dump == "texel")
                        renderer.shadow_map.copy_texel(frame.command_buffer,
                                                       (uint32_t)layer, g_cas1_texel_x,
                                                       g_cas1_texel_y, 0);
                    g_cas1_texel_slot = static_cast<int>(fi);
                }
            }
        }

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
