#include "gfx/HudTextPass.h"
#include "gfx/BufferUtils.h"
#include "gfx/ShaderLoader.h"
#include "core/Log.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>

namespace gfx {
namespace {

bool load_module(VkDevice device, const char* path, VkShaderModule& out) {
    std::vector<unsigned int> code;
    if (!load_shader_source(path, code))
        return false;
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size() * sizeof(unsigned int);
    info.pCode = code.data();
    return vkCreateShaderModule(device, &info, nullptr, &out) == VK_SUCCESS;
}

// Public domain 8x8 ASCII 32–126 (font8x8_basic, Marcel Sondaar / Public Domain).
// Each byte is a row; LSB is the leftmost pixel.
constexpr uint8_t kFont8x8[95][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // !
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // "
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // #
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // $
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // %
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // &
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // '
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // (
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // )
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // *
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // +
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ,
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // -
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // .
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // /
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // 0
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // 1
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // 2
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // 3
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // 4
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // 5
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // 6
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // 7
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // 8
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // 9
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // :
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // ;
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // <
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // =
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // >
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // ?
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // @
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // A
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // B
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // C
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // D
    {0x7F, 0x06, 0x06, 0x3E, 0x06, 0x06, 0x7F, 0x00}, // E
    {0x7F, 0x06, 0x06, 0x3E, 0x06, 0x06, 0x06, 0x00}, // F
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // G
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // H
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // I
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // J
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // K
    {0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x7F, 0x00}, // L
    {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00}, // M
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // N
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // O
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x06, 0x00}, // P
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // Q
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // R
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // S
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // T
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // U
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // V
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // W
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // X
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // Y
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // Z
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // [
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // backslash
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // ]
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // ^
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // _
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // `
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // a
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // b
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // c
    {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00}, // d
    {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00}, // e
    {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0f, 0x00}, // f
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // g
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // h
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // i
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // j
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // k
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // l
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // m
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // n
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // o
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // p
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // q
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // r
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // s
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // t
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // u
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // v
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // w
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // x
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // y
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // z
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // {
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // |
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // }
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ~
};

void raster_atlas(std::vector<uint8_t>& out) {
    constexpr int kPx = 8;
    constexpr int kCols = 16;
    constexpr int kW = 128;
    constexpr int kH = 48;
    out.assign(static_cast<size_t>(kW * kH), 0);
    for (int g = 0; g < 95; ++g) {
        const int col = g % kCols;
        const int row = g / kCols;
        const int ox = col * kPx;
        const int oy = row * kPx;
        for (int y = 0; y < kPx; ++y) {
            const uint8_t bits = kFont8x8[g][y];
            for (int x = 0; x < kPx; ++x) {
                if (bits & (1u << x))
                    out[static_cast<size_t>((oy + y) * kW + (ox + x))] = 255;
            }
        }
    }
}

bool submit_one_shot(VkDevice device, VkQueue queue, VkCommandPool pool,
                     const std::function<void(VkCommandBuffer)>& record) {
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &alloc, &cmd) != VK_SUCCESS)
        return false;
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, pool, 1, &cmd);
        return false;
    }
    record(cmd);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    const bool ok =
        vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
    if (ok)
        vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
    return ok;
}

} // namespace

void HudTextPass::begin_frame() {
    screen_verts_.clear();
    view_verts_.clear();
}

void HudTextPass::add_text(const char* text, float x, float y,
                           float pixel_or_meter_height, const glm::vec4& color,
                           HudSpace space) {
    if (!text || !text[0])
        return;
    if (space == HudSpace::View)
        emit_string(view_verts_, text, x, y, pixel_or_meter_height, color, false);
    else
        emit_string(screen_verts_, text, x, y, pixel_or_meter_height, color, true);
}

void HudTextPass::emit_string(std::vector<Vertex>& out, const char* text, float x,
                             float y, float height, const glm::vec4& color,
                             bool y_down) {
    const float gh = height;
    const float gw = height; // square cells
    const float du = 1.0f / static_cast<float>(kAtlasCols);
    const float dv = 1.0f / static_cast<float>(kAtlasRows);
    float cx = x;
    float cy = y;
    const float line = gh * 1.15f;
    for (const char* p = text; *p; ++p) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c == '\n') {
            cx = x;
            cy += y_down ? line : -line;
            continue;
        }
        if (c == '\t') {
            cx += gw * 4.0f;
            continue;
        }
        if (c < 32 || c > 126)
            c = '?';
        const int gi = static_cast<int>(c) - 32;
        const int col = gi % kAtlasCols;
        const int row = gi / kAtlasCols;
        const float u0 = static_cast<float>(col) * du;
        const float v0 = static_cast<float>(row) * dv;
        const float u1 = u0 + du;
        const float v1 = v0 + dv;
        const float x0 = cx;
        const float x1 = cx + gw;
        const float y0 = cy;
        const float y1 = y_down ? (cy + gh) : (cy - gh);

        Vertex verts[6] = {
            {{x0, y0}, {u0, v0}, color}, {{x1, y0}, {u1, v0}, color},
            {{x1, y1}, {u1, v1}, color}, {{x0, y0}, {u0, v0}, color},
            {{x1, y1}, {u1, v1}, color}, {{x0, y1}, {u0, v1}, color},
        };
        out.insert(out.end(), verts, verts + 6);
        cx += gw;
    }
}

bool HudTextPass::create_overlay_pass(VkDevice device, VkFormat format) {
    VkAttachmentDescription2 att{};
    att.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    att.format = format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference2 color_ref{};
    color_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_ref.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkSubpassDescription2 sub{};
    sub.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &color_ref;

    VkSubpassDependency2 in_dep{};
    in_dep.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    in_dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    in_dep.dstSubpass = 0;
    in_dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    in_dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    in_dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    in_dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    VkSubpassDependency2 out_dep{};
    out_dep.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    out_dep.srcSubpass = 0;
    out_dep.dstSubpass = VK_SUBPASS_EXTERNAL;
    out_dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    out_dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    out_dep.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    out_dep.dstAccessMask = 0;

    VkSubpassDependency2 deps[] = {in_dep, out_dep};
    VkRenderPassCreateInfo2 rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    rp.attachmentCount = 1;
    rp.pAttachments = &att;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 2;
    rp.pDependencies = deps;
    return vkCreateRenderPass2(device, &rp, nullptr, &overlay_rp_) == VK_SUCCESS;
}

bool HudTextPass::create_pipeline(VkDevice device) {
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    if (!load_module(device, "shaders/hud_text.vert.spv", vert) ||
        !load_module(device, "shaders/hud_text.frag.spv", frag)) {
        LOG_ERROR("[HudText] failed to load shaders");
        if (vert)
            vkDestroyShaderModule(device, vert, nullptr);
        if (frag)
            vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    VkDescriptorSetLayoutBinding bind{};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo sl{};
    sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.bindingCount = 1;
    sl.pBindings = &bind;
    if (vkCreateDescriptorSetLayout(device, &sl, nullptr, &set_layout_) !=
        VK_SUCCESS) {
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = sizeof(glm::mat4);
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &set_layout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device, &pl, nullptr, &layout_) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, pos);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, uv);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[2].offset = offsetof(Vertex, color);
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dyn;
    gp.layout = layout_;
    gp.renderPass = overlay_rp_;
    gp.subpass = 0;
    const VkResult pr =
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline_);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    return pr == VK_SUCCESS;
}

bool HudTextPass::create_atlas(VkDevice device, VmaAllocator allocator, VkQueue queue,
                              VkCommandPool pool) {
    std::vector<uint8_t> pixels;
    raster_atlas(pixels);

    VkImageCreateInfo img{};
    img.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img.imageType = VK_IMAGE_TYPE_2D;
    img.format = VK_FORMAT_R8_UNORM;
    img.extent = {static_cast<uint32_t>(kAtlasW), static_cast<uint32_t>(kAtlasH), 1};
    img.mipLevels = 1;
    img.arrayLayers = 1;
    img.samples = VK_SAMPLE_COUNT_1_BIT;
    img.tiling = VK_IMAGE_TILING_OPTIMAL;
    img.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    alloc.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (vmaCreateImage(allocator, &img, &alloc, &atlas_.handle, &atlas_.allocation,
                       &atlas_.info) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = atlas_.handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8_UNORM;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &view, nullptr, &atlas_.view) != VK_SUCCESS)
        return false;

    AllocatedBuffer staging{};
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(pixels.size());
    if (!BufferUtils::initialize_buffer(device, allocator, bytes, staging,
                                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT))
        return false;
    std::memcpy(staging.mapped_data, pixels.data(), pixels.size());

    const bool ok = submit_one_shot(device, queue, pool, [&](VkCommandBuffer cmd) {
        BufferUtils::transition_image_layout(
            cmd, atlas_.handle, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kAtlasW, kAtlasH);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {static_cast<uint32_t>(kAtlasW),
                            static_cast<uint32_t>(kAtlasH), 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, atlas_.handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        BufferUtils::transition_image_layout(
            cmd, atlas_.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kAtlasW, kAtlasH);
    });
    BufferUtils::destroy_buffer(device, allocator, staging);
    if (!ok)
        return false;

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &si, nullptr, &sampler_) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device, &pi, nullptr, &pool_) != VK_SUCCESS)
        return false;
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &set_layout_;
    if (vkAllocateDescriptorSets(device, &ai, &set_) != VK_SUCCESS)
        return false;
    VkDescriptorImageInfo ii{};
    ii.sampler = sampler_;
    ii.imageView = atlas_.view;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wr{};
    wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet = set_;
    wr.dstBinding = 0;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &ii;
    vkUpdateDescriptorSets(device, 1, &wr, 0, nullptr);
    return true;
}

bool HudTextPass::create(VkDevice device, VmaAllocator allocator, VkQueue graphics_queue,
                        VkCommandPool cmd_pool, VkFormat swap_format) {
    destroy(device, allocator);
    device_ = device;
    allocator_ = allocator;
    swap_format_ = swap_format;
    if (!create_overlay_pass(device, swap_format) || !create_pipeline(device) ||
        !create_atlas(device, allocator, graphics_queue, cmd_pool)) {
        LOG_ERROR("[HudText] create failed");
        destroy(device, allocator);
        return false;
    }
    LOG_INFO("[HudText] overlay text ready (8x8 atlas, Screen + View spaces)");
    return true;
}

void HudTextPass::destroy(VkDevice device, VmaAllocator allocator) {
    for (VkFramebuffer fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, fb, nullptr);
    }
    framebuffers_.clear();
    for (uint32_t i = 0; i < kMaxFrames; ++i) {
        BufferUtils::destroy_buffer(device, allocator, vertex_buffers_[i]);
        vertex_capacity_[i] = 0;
    }
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
        set_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (atlas_.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, atlas_.view, nullptr);
        atlas_.view = VK_NULL_HANDLE;
    }
    if (atlas_.handle != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, atlas_.handle, atlas_.allocation);
        atlas_.handle = VK_NULL_HANDLE;
        atlas_.allocation = VK_NULL_HANDLE;
    }
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
    if (set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, set_layout_, nullptr);
        set_layout_ = VK_NULL_HANDLE;
    }
    if (overlay_rp_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, overlay_rp_, nullptr);
        overlay_rp_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
}

bool HudTextPass::set_swapchain(VkDevice device, VkExtent2D extent,
                                const std::vector<VkImageView>& swap_views) {
    for (VkFramebuffer fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, fb, nullptr);
    }
    framebuffers_.assign(swap_views.size(), VK_NULL_HANDLE);
    if (overlay_rp_ == VK_NULL_HANDLE)
        return false;
    for (size_t i = 0; i < swap_views.size(); ++i) {
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = overlay_rp_;
        fi.attachmentCount = 1;
        fi.pAttachments = &swap_views[i];
        fi.width = extent.width;
        fi.height = extent.height;
        fi.layers = 1;
        if (vkCreateFramebuffer(device, &fi, nullptr, &framebuffers_[i]) !=
            VK_SUCCESS) {
            LOG_ERROR("[HudText] framebuffer create failed");
            return false;
        }
    }
    return true;
}

bool HudTextPass::ensure_vertex_capacity(uint32_t frame_index, size_t vertex_count) {
    if (vertex_count == 0 || frame_index >= kMaxFrames)
        return true;
    if (vertex_count <= vertex_capacity_[frame_index] &&
        vertex_buffers_[frame_index].buffer != VK_NULL_HANDLE)
        return true;
    size_t cap = vertex_capacity_[frame_index] > 0 ? vertex_capacity_[frame_index]
                                                   : 1024;
    while (cap < vertex_count)
        cap *= 2;
    AllocatedBuffer next{};
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(cap * sizeof(Vertex));
    if (!BufferUtils::initialize_buffer(device_, allocator_, bytes, next,
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        LOG_ERROR("[HudText] vertex buffer alloc failed");
        return false;
    }
    BufferUtils::destroy_buffer(device_, allocator_, vertex_buffers_[frame_index]);
    vertex_buffers_[frame_index] = next;
    vertex_capacity_[frame_index] = cap;
    return true;
}

void HudTextPass::draw(VkCommandBuffer cmd, uint32_t frame_index, uint32_t image_index,
                      VkExtent2D extent, const glm::mat4& camera_proj,
                      float view_plane_distance) {
    if (!is_ready() || !has_text() || image_index >= framebuffers_.size())
        return;
    if (framebuffers_[image_index] == VK_NULL_HANDLE)
        return;

    std::vector<Vertex> packed;
    packed.reserve(screen_verts_.size() + view_verts_.size());
    packed.insert(packed.end(), screen_verts_.begin(), screen_verts_.end());
    packed.insert(packed.end(), view_verts_.begin(), view_verts_.end());
    if (!ensure_vertex_capacity(frame_index, packed.size()))
        return;
    std::memcpy(vertex_buffers_[frame_index].mapped_data, packed.data(),
                packed.size() * sizeof(Vertex));

    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = overlay_rp_;
    bi.framebuffer = framebuffers_[image_index];
    bi.renderArea.extent = extent;
    vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                            &set_, 0, nullptr);
    VkViewport vp{};
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{};
    sc.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &sc);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffers_[frame_index].buffer, &off);

    const uint32_t n_screen = static_cast<uint32_t>(screen_verts_.size());
    const uint32_t n_view = static_cast<uint32_t>(view_verts_.size());
    if (n_screen > 0) {
        glm::mat4 ortho(1.0f);
        const float w = std::max(static_cast<float>(extent.width), 1.0f);
        const float h = std::max(static_cast<float>(extent.height), 1.0f);
        ortho[0][0] = 2.0f / w;
        ortho[1][1] = 2.0f / h;
        ortho[3][0] = -1.0f;
        ortho[3][1] = -1.0f;
        vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(glm::mat4), &ortho);
        vkCmdDraw(cmd, n_screen, 1, 0, 0);
    }
    if (n_view > 0) {
        // Head-locked plane: camera space, +X right, +Y up, at z = -distance.
        glm::mat4 plane(1.0f);
        plane[3][2] = -std::max(view_plane_distance, 0.05f);
        const glm::mat4 mvp = camera_proj * plane;
        vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(glm::mat4), &mvp);
        vkCmdDraw(cmd, n_view, 1, n_screen, 0);
    }

    vkCmdEndRenderPass(cmd);
}

} // namespace gfx
