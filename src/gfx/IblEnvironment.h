#pragma once

#include "gfx/AllocatedImage.h"
#include "gfx/Light.h"
#include <array>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace gfx {

// Engine-level IBL resources (not part of glTF).
// Default: procedural outdoor sky. Optional: Radiance .hdr equirect via path.
//
// Diffuse: 3-band SH in FrameConstants.shCoefficients
// Specular: prefiltered GGX cubemap + 2D BRDF integration LUT (split-sum)
struct IblEnvironment {
    static constexpr uint32_t kCubeSize = 32;      // face resolution (base mip)
    static constexpr uint32_t kBrdfSize = 128;      // BRDF LUT resolution
    static constexpr uint32_t kPrefilterSamples = 32;
    static constexpr uint32_t kBrdfSamples = 64;

    AllocatedImage prefiltered_cube{};
    AllocatedImage brdf_lut{};
    VkSampler cube_sampler{VK_NULL_HANDLE};
    VkSampler lut_sampler{VK_NULL_HANDLE};

    std::array<glm::vec4, 9> sh_coefficients{};
    uint32_t mip_count = 1;
    bool ready = false;
    bool used_hdr_equirect = false;

    // Optional absolute/relative path to a Radiance .hdr equirectangular map.
    // Set before initialize(); empty → procedural environment.
    std::string equirect_hdr_path;

    // Create GPU images, bake CPU-side, upload. Call once after VMA + queues exist.
    bool initialize(VkDevice device, VmaAllocator allocator, VkQueue graphics_queue,
                    uint32_t graphics_queue_family);

    void destroy(VkDevice device, VmaAllocator allocator);

    // Bind samplerCube (binding_cube) + sampler2D LUT (binding_lut) on a set.
    void bind_descriptors(VkDevice device, VkDescriptorSet set, uint32_t binding_cube,
                          uint32_t binding_lut) const;
};

} // namespace gfx
