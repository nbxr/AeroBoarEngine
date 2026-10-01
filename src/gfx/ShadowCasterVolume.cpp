#include "gfx/ShadowCasterVolume.h"
#include "core/Frustum.h"
#include "gfx/ShadowMap.h"

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace gfx {
namespace {

constexpr float kEps = 1e-5f;

// core::Frustum order: 0 left, 1 right, 2 bottom, 3 top, 4 far (rev-Z), 5 near.
// The 12 edges of a truncated pyramid — not every front/back pair (left×right
// is not a silhouette edge).
constexpr uint32_t kFrustumEdges[12][2] = {
    {5, 0}, {5, 1}, {5, 2}, {5, 3}, // near + sides
    {4, 0}, {4, 1}, {4, 2}, {4, 3}, // far + sides
    {0, 2}, {0, 3}, {1, 2}, {1, 3}, // left/right + bottom/top
};

bool two_plane_line(const glm::vec4& a, const glm::vec4& b, glm::vec3& origin,
                    glm::vec3& dir) {
    const glm::vec3 n1(a);
    const glm::vec3 n2(b);
    dir = glm::cross(n1, n2);
    const float det = glm::dot(dir, dir);
    if (det < 1e-12f)
        return false;
    // Planes are n·x + d = 0. Point on the intersection line:
    origin = (glm::cross(dir, n2) * a.w + glm::cross(n1, dir) * b.w) / det;
    return std::isfinite(origin.x);
}

} // namespace

CasterVolume build_caster_volume(const glm::mat4& receiver_view_proj,
                                 const glm::vec3& to_light) {
    CasterVolume out{};
    glm::vec3 Lto = glm::normalize(to_light);
    if (!std::isfinite(Lto.x) || glm::length(Lto) < 1e-5f)
        return out;
    // Doc L = shadow-ray travel. CPU `to_light` is NdotL (toward the light).
    const glm::vec3 L = -Lto;

    const core::Frustum fr = core::Frustum::from_view_proj(receiver_view_proj);
    glm::vec3 corners[8];
    glm::vec3 interior(0.0f);
    if (ShadowMap::frustum_corners(receiver_view_proj, corners)) {
        for (int i = 0; i < 8; ++i)
            interior += corners[i];
        interior *= 0.125f;
    }

    // Engine planes are inward (dot(n,p)+d >= 0). Doc uses outward N:
    // s_doc = N_out · L = -N_in · L. Front (faces the light) ⇔ N_in · L > 0.
    uint8_t kind[6]; // 0 front (drop), 1 back (keep), 2 grazing (∥ L, keep)
    for (uint32_t i = 0; i < 6; ++i) {
        const float s = glm::dot(glm::vec3(fr.planes[i]), L);
        if (s > kEps)
            kind[i] = 0;
        else if (s < -kEps)
            kind[i] = 1;
        else
            kind[i] = 2;
    }

    auto push_inward = [&](glm::vec4 pl) {
        if (out.count >= kMaxCasterPlanes)
            return;
        const glm::vec3 n(pl);
        const float len = glm::length(n);
        if (len < kEps)
            return;
        pl /= len;
        if (glm::dot(glm::vec3(pl), interior) + pl.w < 0.0f)
            pl = -pl;
        out.planes[out.count++] = pl;
    };

    for (uint32_t i = 0; i < 6; ++i) {
        if (kind[i] != 0)
            push_inward(fr.planes[i]); // back + grazing; drop front
    }

    for (const auto& e : kFrustumEdges) {
        const uint32_t a = e[0];
        const uint32_t b = e[1];
        const bool sil = (kind[a] == 0 && kind[b] == 1) || (kind[a] == 1 && kind[b] == 0);
        if (!sil)
            continue;
        glm::vec3 origin, dir;
        if (!two_plane_line(fr.planes[a], fr.planes[b], origin, dir))
            continue;
        glm::vec3 nclip = glm::cross(dir, L);
        const float len = glm::length(nclip);
        if (len < kEps)
            continue;
        nclip /= len;
        push_inward(glm::vec4(nclip, -glm::dot(nclip, origin)));
    }

    if (out.count == 0) {
        for (uint32_t i = 0; i < 6 && out.count < kMaxCasterPlanes; ++i)
            push_inward(fr.planes[i]);
    }
    return out;
}

glm::mat4 cascade_receiver_view_proj(const glm::mat4& view, float fov_degrees,
                                     float aspect, float znear, float zfar) {
    znear = std::max(znear, 1e-4f);
    zfar = std::max(zfar, znear + 1e-3f);
    // GLM_FORCE_DEPTH_ZERO_TO_ONE: perspective is ndc z 0 at near, 1 at far.
    // The row rewrite turns that into reverse-Z (1 at near, 0 at far).
    glm::mat4 p = glm::perspective(glm::radians(fov_degrees), aspect, znear, zfar);
    for (int c = 0; c < 4; ++c)
        p[c][2] = p[c][3] - p[c][2];
    p[1][1] *= -1.0f;
    return p * view;
}

} // namespace gfx

