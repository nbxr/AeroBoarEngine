#include "gfx/Engine.h"
#include "gfx/AllocatedBuffer.h"
#include "gfx/AllocatedImage.h"
#include "gfx/Renderer.h"
#include "gfx/ShaderLoader.h"

bool gfx::Engine::init_pipeline_layout() {
    // Pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &renderer.vk.descriptor_set_layout;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &renderer.vk.push_constant_range;

    if (vkCreatePipelineLayout(renderer.vk.device, &pipeline_layout_info,
                               nullptr,
                               &renderer.vk.pipeline_layout) != VK_SUCCESS) {
        LOG_ERROR("Failed to create pipeline layout");
        return false;
    }
    return true;
}

bool gfx::Engine::init_graphics_pipeline() {
    std::vector<unsigned int> vertex_code;
    std::vector<unsigned int> fragment_code;

    // Real PBR shaders (now that GLTF loading reliably supplies per-primitive materials + textures)
    if (!load_shader_source("shaders/pbr.vert.spv", vertex_code)) {
        LOG_ERROR("Failed to load vertex shader");
        return false;
    }

    if (!load_shader_source("shaders/pbr.frag.spv", fragment_code)) {
        LOG_ERROR("Failed to load fragment shader");
        return false;
    }

    VkShaderModule vertex_shader_module = {};
    VkShaderModuleCreateInfo vertex_shader_info = {};
    vertex_shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_shader_info.codeSize = vertex_code.size() * sizeof(unsigned int);
    vertex_shader_info.pCode = vertex_code.data();

    if (vkCreateShaderModule(renderer.vk.device, &vertex_shader_info, nullptr,
                             &vertex_shader_module) != VK_SUCCESS) {
        LOG_ERROR("Failed to create vertex shader module");
        return false;
    }

    VkShaderModule fragment_shader_module = {};
    VkShaderModuleCreateInfo fragment_shader_info = {};
    fragment_shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_shader_info.codeSize = fragment_code.size() * sizeof(unsigned int);
    fragment_shader_info.pCode = fragment_code.data();

    if (vkCreateShaderModule(renderer.vk.device, &fragment_shader_info, nullptr,
                             &fragment_shader_module) != VK_SUCCESS) {
        vkDestroyShaderModule(renderer.vk.device, vertex_shader_module,
                              nullptr);
        LOG_ERROR("Failed to create fragment shader module");
        return false;
    }

    VkPipelineShaderStageCreateInfo vertex_stage_info = {};
    vertex_stage_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertex_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_stage_info.module = vertex_shader_module;
    vertex_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo fragment_stage_info = {};
    fragment_stage_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragment_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragment_stage_info.module = fragment_shader_module;
    fragment_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vertex_stage_info,
                                                       fragment_stage_info};

    // Vertex input state — proper attributes (replaces legacy SSBO vertex pulling)
    // Matches gfx::Vertex exactly (stride 56):
    //   loc 0: position (offset 0)
    //   loc 1: normal   (offset 12)
    //   loc 2: tangent  (offset 24)
    //   loc 3: UV0 bits (offset 40, first 4 bytes of the packed uv[8] field)
    static const VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = 56,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    static const VkVertexInputAttributeDescription attr_descs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0  },  // position
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 12 },  // normal
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 24 }, // tangent
        { .location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 40 }   // UV0 (packed bits)
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_desc;
    vertex_input_info.vertexAttributeDescriptionCount = 4;
    vertex_input_info.pVertexAttributeDescriptions = attr_descs;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)renderer.vk.swap_chain_extent.width;
    viewport.height = (float)renderer.vk.swap_chain_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = renderer.vk.swap_chain_extent;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = renderer.vk.msaa_color;
    multisampling.minSampleShading = 1.0f;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    // Color blending
    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;

    // Dynamic state
    std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount =
        static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    // Pipeline creation
    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;

    // Basic depth state to match the render pass
    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable = VK_FALSE;

    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = renderer.vk.pipeline_layout;
    pipeline_info.renderPass = renderer.main_pass.render_pass;
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;
    pipeline_info.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(renderer.vk.device, VK_NULL_HANDLE, 1,
                                  &pipeline_info, nullptr,
                                  &renderer.vk.pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(renderer.vk.device, vertex_shader_module,
                              nullptr);
        vkDestroyShaderModule(renderer.vk.device, fragment_shader_module,
                              nullptr);
        LOG_ERROR("Failed to create graphics pipeline");
        return false;
    }

    vkDestroyShaderModule(renderer.vk.device, vertex_shader_module, nullptr);
    vkDestroyShaderModule(renderer.vk.device, fragment_shader_module, nullptr);

    return true;
}

bool gfx::Engine::init_depth_prepass_pipeline() {
    std::vector<unsigned int> vertex_code;
    if (!load_shader_source("shaders/pbr.vert.spv", vertex_code)) {
        LOG_ERROR("Failed to load vertex shader for depth prepass");
        return false;
    }

    VkShaderModule vertex_shader_module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo vertex_shader_info{};
    vertex_shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_shader_info.codeSize = vertex_code.size() * sizeof(unsigned int);
    vertex_shader_info.pCode = vertex_code.data();
    if (vkCreateShaderModule(renderer.vk.device, &vertex_shader_info, nullptr,
                             &vertex_shader_module) != VK_SUCCESS) {
        LOG_ERROR("Failed to create depth-prepass vertex module");
        return false;
    }

    VkPipelineShaderStageCreateInfo vertex_stage{};
    vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_stage.module = vertex_shader_module;
    vertex_stage.pName = "main";

    // Same vertex layout as PBR (position drives depth; other attrs unused but bound).
    static const VkVertexInputBindingDescription binding_desc = {
        .binding = 0, .stride = 56, .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};

    static const VkVertexInputAttributeDescription attr_descs[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 12},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 24},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 40}};

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_desc;
    vertex_input_info.vertexAttributeDescriptionCount = 4;
    vertex_input_info.pVertexAttributeDescriptions = attr_descs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // No color attachments.
    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 0;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

    std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 1;
    pipeline_info.pStages = &vertex_stage;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = renderer.vk.pipeline_layout;
    pipeline_info.renderPass = renderer.depth_prepass.render_pass;
    pipeline_info.subpass = 0;

    if (vkCreateGraphicsPipelines(renderer.vk.device, VK_NULL_HANDLE, 1, &pipeline_info,
                                  nullptr,
                                  &renderer.vk.depth_prepass_pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(renderer.vk.device, vertex_shader_module, nullptr);
        LOG_ERROR("Failed to create depth-prepass pipeline");
        return false;
    }

    vkDestroyShaderModule(renderer.vk.device, vertex_shader_module, nullptr);
    return true;
}