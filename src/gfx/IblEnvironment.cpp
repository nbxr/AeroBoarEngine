#include "gfx/IblEnvironment.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace gfx {
namespace {

constexpr float PI = 3.14159265359f;

float radical_inverse_vdc(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

glm::vec2 hammersley(uint32_t i, uint32_t n) {
    return {float(i) / float(n), radical_inverse_vdc(i)};
}

glm::vec3 importance_sample_ggx(glm::vec2 xi, float roughness, glm::vec3 N) {
    float a = roughness * roughness;
    float phi = 2.0f * PI * xi.x;
    float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));

    glm::vec3 H(std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta);

    glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    glm::vec3 tangent = glm::normalize(glm::cross(up, N));
    glm::vec3 bitangent = glm::cross(N, tangent);
    return glm::normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float geometry_schlick_ggx(float NdotV, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0f; // IBL k
    return NdotV / (NdotV * (1.0f - k) + k);
}

float geometry_smith(float NdotV, float NdotL, float roughness) {
    return geometry_schlick_ggx(NdotV, roughness) * geometry_schlick_ggx(NdotL, roughness);
}

// Procedural outdoor environment (sky + sun). HDR-ish values for specular.
glm::vec3 sample_environment(glm::vec3 dir) {
    dir = glm::normalize(dir);
    const glm::vec3 sun_dir = glm::normalize(glm::vec3(0.35f, 0.85f, 0.25f));
    const glm::vec3 zenith(0.18f, 0.28f, 0.55f);
    const glm::vec3 horizon(0.55f, 0.48f, 0.40f);
    const glm::vec3 ground(0.08f, 0.07f, 0.06f);

    float t = dir.y * 0.5f + 0.5f;
    glm::vec3 sky = glm::mix(horizon, zenith, std::clamp(t, 0.0f, 1.0f));
    if (dir.y < 0.0f) {
        float g = std::clamp(-dir.y, 0.0f, 1.0f);
        sky = glm::mix(horizon, ground, g);
    }

    float sun = std::pow(std::max(glm::dot(dir, sun_dir), 0.0f), 256.0f);
    sky += glm::vec3(1.0f, 0.95f, 0.85f) * sun * 8.0f;
    // Soft sun glow
    float glow = std::pow(std::max(glm::dot(dir, sun_dir), 0.0f), 32.0f);
    sky += glm::vec3(1.0f, 0.9f, 0.7f) * glow * 0.35f;
    return sky;
}

glm::vec3 cubemap_direction(uint32_t face, float u, float v) {
    // u,v in [-1,1]
    switch (face) {
    case 0: return glm::normalize(glm::vec3(1.0f, -v, -u));  // +X
    case 1: return glm::normalize(glm::vec3(-1.0f, -v, u)); // -X
    case 2: return glm::normalize(glm::vec3(u, 1.0f, v));   // +Y
    case 3: return glm::normalize(glm::vec3(u, -1.0f, -v)); // -Y
    case 4: return glm::normalize(glm::vec3(u, -v, 1.0f));  // +Z
    default: return glm::normalize(glm::vec3(-u, -v, -1.0f)); // -Z
    }
}

glm::vec3 prefilter_env(glm::vec3 R, float roughness) {
    glm::vec3 N = R;
    glm::vec3 V = R;
    glm::vec3 color(0.0f);
    float total_weight = 0.0f;

    const uint32_t samples = IblEnvironment::kPrefilterSamples;
    for (uint32_t i = 0; i < samples; ++i) {
        glm::vec2 xi = hammersley(i, samples);
        glm::vec3 H = importance_sample_ggx(xi, roughness, N);
        glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

        float NdotL = std::max(glm::dot(N, L), 0.0f);
        if (NdotL > 0.0f) {
            color += sample_environment(L) * NdotL;
            total_weight += NdotL;
        }
    }
    return total_weight > 0.0f ? color / total_weight : color;
}

glm::vec2 integrate_brdf(float NdotV, float roughness) {
    glm::vec3 V(std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
    float A = 0.0f;
    float B = 0.0f;
    glm::vec3 N(0.0f, 0.0f, 1.0f);

    const uint32_t samples = IblEnvironment::kBrdfSamples;
    for (uint32_t i = 0; i < samples; ++i) {
        glm::vec2 xi = hammersley(i, samples);
        glm::vec3 H = importance_sample_ggx(xi, roughness, N);
        glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

        float NdotL = std::max(L.z, 0.0f);
        float NdotH = std::max(H.z, 0.0f);
        float VdotH = std::max(glm::dot(V, H), 0.0f);

        if (NdotL > 0.0f) {
            float G = geometry_smith(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / std::max(NdotH * NdotV, 1e-4f);
            float Fc = std::pow(1.0f - VdotH, 5.0f);
            A += (1.0f - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(samples);
    B /= float(samples);
    return {A, B};
}

// Project environment onto 3-band SH (Ramamoorthi-style sampling over sphere).
void bake_sh(std::array<glm::vec4, 9>& out_sh) {
    out_sh.fill(glm::vec4(0.0f));
    const int samples = 64;
    float weight_sum = 0.0f;

    for (int i = 0; i < samples; ++i) {
        for (int j = 0; j < samples; ++j) {
            float u = (i + 0.5f) / samples;
            float v = (j + 0.5f) / samples;
            float theta = 2.0f * std::acos(std::sqrt(1.0f - u));
            float phi = 2.0f * PI * v;
            glm::vec3 dir(std::sin(theta) * std::cos(phi), std::cos(theta),
                          std::sin(theta) * std::sin(phi));
            glm::vec3 col = sample_environment(dir);
            float pdf = 1.0f / (4.0f * PI);
            float w = (1.0f / (samples * samples)) / pdf;
            weight_sum += w;

            // Real SH basis (order 2), scaled for irradiance convolution later
            float x = dir.x, y = dir.y, z = dir.z;
            float basis[9] = {
                0.282095f,
                0.488603f * y,
                0.488603f * z,
                0.488603f * x,
                1.092548f * x * y,
                1.092548f * y * z,
                0.315392f * (3.0f * z * z - 1.0f),
                1.092548f * x * z,
                0.546274f * (x * x - y * y),
            };
            for (int k = 0; k < 9; ++k) {
                out_sh[k] += glm::vec4(col * basis[k] * w, 0.0f);
            }
        }
    }

    // Convert radiance SH → irradiance (cosine lobe A_l factors)
    const float A[3] = {PI, 2.094395f, 0.785398f}; // A0, A1, A2
    out_sh[0] *= A[0];
    for (int k = 1; k <= 3; ++k)
        out_sh[k] *= A[1];
    for (int k = 4; k <= 8; ++k)
        out_sh[k] *= A[2];

    // Scale down — procedural sky is fairly bright; keep ambient subtle under direct lights
    for (auto& c : out_sh)
        c *= 0.15f;
}

bool create_staging(VkDevice device, VmaAllocator allocator, VkDeviceSize size,
                    VkBuffer& buffer, VmaAllocation& alloc, void** mapped) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ac{};
    ac.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    ac.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo info{};
    if (vmaCreateBuffer(allocator, &bi, &ac, &buffer, &alloc, &info) != VK_SUCCESS)
        return false;
    *mapped = info.pMappedData;
    return *mapped != nullptr;
}

void cmd_transition(VkCommandBuffer cmd, VkImage image, VkImageLayout old_l,
                    VkImageLayout new_l, uint32_t mips, uint32_t layers,
                    VkAccessFlags src_a, VkAccessFlags dst_a,
                    VkPipelineStageFlags src_s, VkPipelineStageFlags dst_s) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = old_l;
    b.newLayout = new_l;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = mips;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = layers;
    b.srcAccessMask = src_a;
    b.dstAccessMask = dst_a;
    vkCmdPipelineBarrier(cmd, src_s, dst_s, 0, 0, nullptr, 0, nullptr, 1, &b);
}

} // namespace

bool IblEnvironment::initialize(VkDevice device, VmaAllocator allocator,
                                VkQueue graphics_queue,
                                uint32_t graphics_queue_family) {
    destroy(device, allocator);

    mip_count = 1;
    uint32_t s = kCubeSize;
    while (s > 4) {
        s >>= 1;
        ++mip_count;
    }

    LOG_INFO("[IBL] Baking procedural environment (cube " << kCubeSize << "^2, "
             << mip_count << " mips, BRDF " << kBrdfSize << "^2)...");

    bake_sh(sh_coefficients);

    // ---- Build prefiltered cubemap pixels (all mips, all faces) ----
    std::vector<float> cube_pixels;
    // Worst-case size estimate
    cube_pixels.reserve(6 * kCubeSize * kCubeSize * 4 * 2);

    struct MipLevel {
        uint32_t size;
        size_t offset_floats; // into cube_pixels
    };
    std::vector<MipLevel> mips(mip_count);

    size_t float_cursor = 0;
    for (uint32_t m = 0; m < mip_count; ++m) {
        uint32_t face_size = std::max(1u, kCubeSize >> m);
        mips[m] = {face_size, float_cursor};
        float roughness = (mip_count == 1) ? 0.0f : float(m) / float(mip_count - 1);

        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t y = 0; y < face_size; ++y) {
                for (uint32_t x = 0; x < face_size; ++x) {
                    float u = (x + 0.5f) / face_size * 2.0f - 1.0f;
                    float v = (y + 0.5f) / face_size * 2.0f - 1.0f;
                    glm::vec3 dir = cubemap_direction(face, u, v);
                    glm::vec3 col = (m == 0 && roughness < 0.01f)
                                        ? sample_environment(dir)
                                        : prefilter_env(dir, std::max(roughness, 0.04f));
                    cube_pixels.push_back(col.r);
                    cube_pixels.push_back(col.g);
                    cube_pixels.push_back(col.b);
                    cube_pixels.push_back(1.0f);
                    float_cursor += 4;
                }
            }
        }
    }

    // ---- BRDF LUT ----
    std::vector<float> lut_pixels(kBrdfSize * kBrdfSize * 2);
    for (uint32_t y = 0; y < kBrdfSize; ++y) {
        for (uint32_t x = 0; x < kBrdfSize; ++x) {
            float NdotV = (x + 0.5f) / kBrdfSize;
            float roughness = (y + 0.5f) / kBrdfSize;
            NdotV = std::max(NdotV, 0.001f);
            glm::vec2 ab = integrate_brdf(NdotV, roughness);
            size_t idx = (size_t(y) * kBrdfSize + x) * 2;
            lut_pixels[idx + 0] = ab.x;
            lut_pixels[idx + 1] = ab.y;
        }
    }

    // ---- Create GPU images ----
    VkImageCreateInfo cube_info{};
    cube_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    cube_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    cube_info.imageType = VK_IMAGE_TYPE_2D;
    cube_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    cube_info.extent = {kCubeSize, kCubeSize, 1};
    cube_info.mipLevels = mip_count;
    cube_info.arrayLayers = 6;
    cube_info.samples = VK_SAMPLE_COUNT_1_BIT;
    cube_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    cube_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    cube_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    cube_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo img_alloc{};
    img_alloc.usage = VMA_MEMORY_USAGE_AUTO;
    img_alloc.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    if (vmaCreateImage(allocator, &cube_info, &img_alloc, &prefiltered_cube.handle,
                       &prefiltered_cube.allocation, &prefiltered_cube.info) != VK_SUCCESS) {
        LOG_ERROR("[IBL] Failed to create prefiltered cubemap image");
        return false;
    }

    VkImageViewCreateInfo cube_view{};
    cube_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    cube_view.image = prefiltered_cube.handle;
    cube_view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cube_view.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    cube_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cube_view.subresourceRange.levelCount = mip_count;
    cube_view.subresourceRange.layerCount = 6;
    if (vkCreateImageView(device, &cube_view, nullptr, &prefiltered_cube.view) != VK_SUCCESS) {
        LOG_ERROR("[IBL] Failed to create cubemap view");
        return false;
    }

    VkImageCreateInfo lut_info{};
    lut_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    lut_info.imageType = VK_IMAGE_TYPE_2D;
    lut_info.format = VK_FORMAT_R16G16_SFLOAT;
    lut_info.extent = {kBrdfSize, kBrdfSize, 1};
    lut_info.mipLevels = 1;
    lut_info.arrayLayers = 1;
    lut_info.samples = VK_SAMPLE_COUNT_1_BIT;
    lut_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    lut_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    lut_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    lut_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vmaCreateImage(allocator, &lut_info, &img_alloc, &brdf_lut.handle, &brdf_lut.allocation,
                       &brdf_lut.info) != VK_SUCCESS) {
        LOG_ERROR("[IBL] Failed to create BRDF LUT image");
        return false;
    }

    VkImageViewCreateInfo lut_view{};
    lut_view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    lut_view.image = brdf_lut.handle;
    lut_view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    lut_view.format = VK_FORMAT_R16G16_SFLOAT;
    lut_view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    lut_view.subresourceRange.levelCount = 1;
    lut_view.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &lut_view, nullptr, &brdf_lut.view) != VK_SUCCESS) {
        LOG_ERROR("[IBL] Failed to create BRDF LUT view");
        return false;
    }

    // Convert float32 bake → float16 on the fly when uploading via staging as float32
    // (driver converts on copy if formats match — we use R32 staging and copy; easier path:
    //  upload as R32G32B32A32 then... no, image is R16. Convert manually.)
    auto f32_to_f16 = [](float f) -> uint16_t {
        // Simple float32→float16 (IEEE)
        uint32_t x;
        std::memcpy(&x, &f, 4);
        uint32_t sign = (x >> 16) & 0x8000;
        int32_t exp = int32_t((x >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = x & 0x7FFFFF;
        if (exp <= 0) {
            if (exp < -10)
                return uint16_t(sign);
            mant |= 0x800000;
            int t = 14 - exp;
            uint32_t m = mant >> t;
            return uint16_t(sign | m);
        }
        if (exp >= 31)
            return uint16_t(sign | 0x7C00);
        return uint16_t(sign | (exp << 10) | (mant >> 13));
    };

    std::vector<uint16_t> cube_f16(cube_pixels.size());
    for (size_t i = 0; i < cube_pixels.size(); ++i)
        cube_f16[i] = f32_to_f16(cube_pixels[i]);

    std::vector<uint16_t> lut_f16(lut_pixels.size());
    for (size_t i = 0; i < lut_pixels.size(); ++i)
        lut_f16[i] = f32_to_f16(lut_pixels[i]);

    VkDeviceSize cube_bytes = cube_f16.size() * sizeof(uint16_t);
    VkDeviceSize lut_bytes = lut_f16.size() * sizeof(uint16_t);

    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation staging_alloc = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!create_staging(device, allocator, cube_bytes + lut_bytes, staging, staging_alloc,
                        &mapped)) {
        LOG_ERROR("[IBL] Staging buffer failed");
        return false;
    }
    std::memcpy(mapped, cube_f16.data(), cube_bytes);
    std::memcpy(static_cast<char*>(mapped) + cube_bytes, lut_f16.data(), lut_bytes);

    // One-shot command buffer
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_ci.queueFamilyIndex = graphics_queue_family;
    if (vkCreateCommandPool(device, &pool_ci, nullptr, &pool) != VK_SUCCESS)
        return false;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &cai, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    cmd_transition(cmd, prefiltered_cube.handle, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mip_count, 6, 0,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
    cmd_transition(cmd, brdf_lut.handle, VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 1, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // Copy cube mips/faces
    VkDeviceSize offset = 0;
    for (uint32_t m = 0; m < mip_count; ++m) {
        uint32_t face_size = mips[m].size;
        VkDeviceSize face_bytes = VkDeviceSize(face_size) * face_size * 4 * sizeof(uint16_t);
        for (uint32_t face = 0; face < 6; ++face) {
            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = m;
            region.imageSubresource.baseArrayLayer = face;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {face_size, face_size, 1};
            vkCmdCopyBufferToImage(cmd, staging, prefiltered_cube.handle,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            offset += face_bytes;
        }
    }

    VkBufferImageCopy lut_region{};
    lut_region.bufferOffset = cube_bytes;
    lut_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    lut_region.imageSubresource.layerCount = 1;
    lut_region.imageExtent = {kBrdfSize, kBrdfSize, 1};
    vkCmdCopyBufferToImage(cmd, staging, brdf_lut.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &lut_region);

    cmd_transition(cmd, prefiltered_cube.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mip_count, 6,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    cmd_transition(cmd, brdf_lut.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, 1, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);

    vkFreeCommandBuffers(device, pool, 1, &cmd);
    vkDestroyCommandPool(device, pool, nullptr);
    vmaDestroyBuffer(allocator, staging, staging_alloc);

    // Samplers
    VkSamplerCreateInfo cube_samp{};
    cube_samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    cube_samp.magFilter = VK_FILTER_LINEAR;
    cube_samp.minFilter = VK_FILTER_LINEAR;
    cube_samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    cube_samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cube_samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cube_samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    cube_samp.minLod = 0.0f;
    cube_samp.maxLod = float(mip_count);
    cube_samp.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    if (vkCreateSampler(device, &cube_samp, nullptr, &cube_sampler) != VK_SUCCESS)
        return false;

    VkSamplerCreateInfo lut_samp = cube_samp;
    lut_samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    lut_samp.maxLod = 0.0f;
    if (vkCreateSampler(device, &lut_samp, nullptr, &lut_sampler) != VK_SUCCESS)
        return false;

    ready = true;
    LOG_INFO("[IBL] Ready (SH L0 rgb ≈ "
             << sh_coefficients[0].r << ", " << sh_coefficients[0].g << ", "
             << sh_coefficients[0].b << ")");
    return true;
}

void IblEnvironment::destroy(VkDevice device, VmaAllocator allocator) {
    if (cube_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, cube_sampler, nullptr);
        cube_sampler = VK_NULL_HANDLE;
    }
    if (lut_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, lut_sampler, nullptr);
        lut_sampler = VK_NULL_HANDLE;
    }
    if (prefiltered_cube.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, prefiltered_cube.view, nullptr);
        prefiltered_cube.view = VK_NULL_HANDLE;
    }
    if (prefiltered_cube.handle != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, prefiltered_cube.handle, prefiltered_cube.allocation);
        prefiltered_cube = {};
    }
    if (brdf_lut.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, brdf_lut.view, nullptr);
        brdf_lut.view = VK_NULL_HANDLE;
    }
    if (brdf_lut.handle != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, brdf_lut.handle, brdf_lut.allocation);
        brdf_lut = {};
    }
    ready = false;
}

void IblEnvironment::bind_descriptors(VkDevice device, VkDescriptorSet set,
                                      uint32_t binding_cube, uint32_t binding_lut) const {
    if (!ready || set == VK_NULL_HANDLE)
        return;

    VkDescriptorImageInfo cube_info{};
    cube_info.sampler = cube_sampler;
    cube_info.imageView = prefiltered_cube.view;
    cube_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo lut_info{};
    lut_info.sampler = lut_sampler;
    lut_info.imageView = brdf_lut.view;
    lut_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = binding_cube;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &cube_info;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = binding_lut;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &lut_info;

    vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
}

} // namespace gfx
