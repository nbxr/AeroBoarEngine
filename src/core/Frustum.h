#pragma once

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include "core/AABB.h"
#include <array>
#include <cmath>
#include <glm/glm.hpp>

namespace core {

// View frustum as 6 planes. Plane: dot(n, p) + d >= 0 ⇒ inside / on plane.
//
// Extraction uses *rows* of the column-major view-projection matrix (Gribb/Hartmann).
// For Vulkan/GLM with depth in [0, 1], the near plane is row2 (not row3+row2 as in
// OpenGL's [-1,1] clip space). Using the OpenGL near formula pushes the near plane
// out and culls objects as the camera approaches them.
struct Frustum {
    std::array<glm::vec4, 6> planes{};

    static Frustum from_view_proj(const glm::mat4& vp) {
        Frustum f;

        // Row r of column-major GLM mat4: (vp[0][r], vp[1][r], vp[2][r], vp[3][r])
        auto row = [&](int r) {
            return glm::vec4(vp[0][r], vp[1][r], vp[2][r], vp[3][r]);
        };
        const glm::vec4 r0 = row(0);
        const glm::vec4 r1 = row(1);
        const glm::vec4 r2 = row(2);
        const glm::vec4 r3 = row(3);

        f.planes[0] = r3 + r0; // left
        f.planes[1] = r3 - r0; // right
        f.planes[2] = r3 + r1; // bottom
        f.planes[3] = r3 - r1; // top
        // Depth 0..1 (GLM_FORCE_DEPTH_ZERO_TO_ONE): near = z_clip >= 0 → row2
        f.planes[4] = r2;      // near
        f.planes[5] = r3 - r2; // far

        for (auto& p : f.planes) {
            const float len = glm::length(glm::vec3(p));
            if (len > 1e-8f)
                p /= len;
        }
        return f;
    }

    // True unless the AABB is completely outside the frustum.
    [[nodiscard]] bool intersects_aabb(const AABB& box) const {
        if (!box.is_valid())
            return true;

        // Pad so edge-on / partially-on-screen bounds are not dropped early.
        const glm::vec3 ext = box.extents();
        const glm::vec3 pad = ext * 0.05f + glm::vec3(0.05f);
        const AABB expanded{box.min - pad, box.max + pad};

        for (const auto& plane : planes) {
            const glm::vec3 n(plane);
            // Positive vertex: corner most aligned with the plane normal
            const glm::vec3 p(
                n.x >= 0.0f ? expanded.max.x : expanded.min.x,
                n.y >= 0.0f ? expanded.max.y : expanded.min.y,
                n.z >= 0.0f ? expanded.max.z : expanded.min.z);
            // Soft bias on near-side tests reduces "pop" when walking into objects
            if (glm::dot(n, p) + plane.w < -0.02f)
                return false;
        }
        return true;
    }
};

} // namespace core
