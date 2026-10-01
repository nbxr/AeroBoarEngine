#pragma once

// Before glm: cascade_receiver_view_proj rewrites a [0,1] projection into
// reverse-Z. If this header is the first glm include, OpenGL [-1,1] makes
// the cut-pyramid near plane the wrong distance and pbr.frag's split does
// not match the receiver volume (shadow-caster.md §11.4).
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RADIANS

#include <cstdint>
#include <glm/glm.hpp>

namespace gfx {

static constexpr uint32_t kMaxCasterPlanes = 16;

// Convex caster volume: back planes of the receiver pyramid + silhouette
// clip planes parallel to the light (docs/architecture/shadow-caster.md).
struct CasterVolume {
    glm::vec4 planes[kMaxCasterPlanes]{}; // n.xyz, d; inside: dot(n,p)+d >= 0
    uint32_t count = 0;
};

// `to_light` is the NdotL vector (toward the light). `receiver_view_proj` is
// the camera view × reverse-Z projection for this cascade's cut pyramid.
CasterVolume build_caster_volume(const glm::mat4& receiver_view_proj,
                                 const glm::vec3& to_light);

// Reverse-Z Y-flipped perspective for one cascade slice (matches Engine::render).
glm::mat4 cascade_receiver_view_proj(const glm::mat4& view, float fov_degrees,
                                     float aspect, float znear, float zfar);

} // namespace gfx

