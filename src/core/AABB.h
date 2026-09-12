#pragma once

// GLM configuration for Vulkan
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include "glm/glm.hpp"
#include <cfloat>
#include <algorithm>

namespace core {
struct AABB {
    glm::vec3 min{ FLT_MAX };
    glm::vec3 max{-FLT_MAX };

    // Returns true if this AABB contains valid data (min <= max on all axes)
    [[nodiscard]] bool is_valid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z &&
               min.x != FLT_MAX && max.x != -FLT_MAX;
    }

    // Center of the box
    [[nodiscard]] glm::vec3 center() const {
        return (min + max) * 0.5f;
    }

    // Full extents (size) of the box
    [[nodiscard]] glm::vec3 extents() const {
        return max - min;
    }

    void expand(const AABB& other) {
        if (!other.is_valid())
            return;
        expand(other.min);
        expand(other.max);
    }

    // Expand this AABB to include the given point
    void expand(const glm::vec3& point) {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);

        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }

    // Return a new AABB that is the conservative bound of this box after
    // being transformed by the given matrix (handles rotation/scale/translation).
    [[nodiscard]] AABB transformed(const glm::mat4& matrix) const {
        if (!is_valid()) return *this;

        AABB result;
        // Transform all 8 corners
        const glm::vec3 corners[8] = {
            {min.x, min.y, min.z},
            {min.x, min.y, max.z},
            {min.x, max.y, min.z},
            {min.x, max.y, max.z},
            {max.x, min.y, min.z},
            {max.x, min.y, max.z},
            {max.x, max.y, min.z},
            {max.x, max.y, max.z}
        };

        for (int i = 0; i < 8; ++i) {
            glm::vec4 p = matrix * glm::vec4(corners[i], 1.0f);
            result.expand(glm::vec3(p));
        }
        return result;
    }
};
}; // namespace core