#pragma once
#include "glm/glm.hpp"

namespace core {
struct AABB {
    glm::vec3 min{ FLT_MAX };
    glm::vec3 max{-FLT_MAX };

    // Helper functions (inline where possible for perf)
    // void expand(const glm::vec3& point);
    // void transform(const glm::mat4& matrix);   // for world AABB from local + transform
    // bool isValid() const;
    // glm::vec3 center() const;
    // glm::vec3 extents() const;
};
}; // namespace core