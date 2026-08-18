#include "gfx/DebugLinePass.h"
#include "gfx/BufferUtils.h"
#include "gfx/Depth.h"
#include "gfx/ShaderLoader.h"
#include "core/Log.h"
#include <cstring>

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

} // namespace

bool DebugLinePass::create(VkDevice device, VmaAllocator allocator,
                           VkRenderPass render_pass,
                           VkSampleCountFlagBits samples) {
    destroy(device, allocator);
    device_ = device;
    allocator_ = allocator;

    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    if (!load_module(device, "shaders/debug_line.vert.spv", vert) ||
        !load_module(device, "shaders/debug_line.frag.spv", frag)) {
        LOG_ERROR("[DebugLine] failed to load shaders");
        if (vert)
            vkDestroyShaderModule(device, vert, nullptr);
        if (frag)
            vkDestroyShaderModule(device, frag, nullptr);
        return false;
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = sizeof(glm::mat4);

    VkPipelineLayoutCreateInfo layout_info{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device, &layout_info, nullptr, &layout_) !=
        VK_SUCCESS) {
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        LOG_ERROR("[DebugLine] pipeline layout failed");
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
    binding.stride = sizeof(physics::DebugVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = offsetof(physics::DebugVertex, position);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attrs[1].offset = offsetof(physics::DebugVertex, color);

    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

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
    ms.rasterizationSamples = samples;

    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = gfx::kDepthCompareLequal;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;

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
    gp.renderPass = render_pass;
    gp.subpass = 0;

    const VkResult pr =
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline_);
    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);
    if (pr != VK_SUCCESS) {
        LOG_ERROR("[DebugLine] pipeline create failed");
        destroy(device, allocator);
        return false;
    }

    LOG_INFO("[DebugLine] physics debug line pipeline ready");
    return true;
}

void DebugLinePass::destroy(VkDevice device, VmaAllocator allocator) {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < kMaxFrames; ++i) {
        BufferUtils::destroy_buffer(device, allocator, vertex_buffers_[i]);
        vertex_capacity_[i] = 0;
    }
    device_ = VK_NULL_HANDLE;
    allocator_ = nullptr;
}

bool DebugLinePass::ensure_vertex_capacity(VkDevice device, VmaAllocator allocator,
                                           uint32_t frame_index,
                                           size_t vertex_count) {
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
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(cap * sizeof(physics::DebugVertex));
    if (!BufferUtils::initialize_buffer(device, allocator, bytes, next,
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
        LOG_ERROR("[DebugLine] vertex buffer alloc failed");
        return false;
    }
    // Only this FIF slot's fence has been waited; do not touch the other slot.
    BufferUtils::destroy_buffer(device, allocator, vertex_buffers_[frame_index]);
    vertex_buffers_[frame_index] = next;
    vertex_capacity_[frame_index] = cap;
    return true;
}

void DebugLinePass::draw(VkCommandBuffer cmd, uint32_t frame_index,
                         VkExtent2D extent, const glm::mat4& view_proj,
                         const std::vector<physics::DebugVertex>& vertices) {
    if (!is_ready() || vertices.empty() || !device_ || !allocator_)
        return;
    if (frame_index >= kMaxFrames)
        return;
    if (!ensure_vertex_capacity(device_, allocator_, frame_index, vertices.size()))
        return;

    auto& vb = vertex_buffers_[frame_index];
    const size_t bytes = vertices.size() * sizeof(physics::DebugVertex);
    std::memcpy(vb.mapped_data, vertices.data(), bytes);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

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

    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(glm::mat4), &view_proj);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb.buffer, &offset);
    vkCmdDraw(cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
}

} // namespace gfx
