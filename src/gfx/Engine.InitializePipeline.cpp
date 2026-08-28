#include "gfx/Engine.h"
#include "gfx/AllocatedBuffer.h"
#include "gfx/AllocatedImage.h"
#include "gfx/Depth.h"
#include "gfx/Renderer.h"
#include "gfx/ShaderLoader.h"
#include "gfx/Vertex.h"

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

    // Vertex input — gfx::Vertex stride 64:
    //   loc 0: position | 1: normal | 2: tangent | 3: UV0/UV1 half
    //   loc 4: color (unorm8) | 5: weights (unorm8) | 6: joints (u8)
    static const VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = static_cast<uint32_t>(gfx::Vertex::stride),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    static const VkVertexInputAttributeDescription attr_descs[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0  },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 12 },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 24 },
        { .location = 3, .binding = 0, .format = VK_FORMAT_R16G16B16A16_SFLOAT, .offset = 40 },
        { .location = 4, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 48 },
        { .location = 5, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 52 },
        { .location = 6, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UINT, .offset = 56 },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_desc;
    vertex_input_info.vertexAttributeDescriptionCount = 7;
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
    // No cull: glTF doubleSided is common (AlphaBlendModeTest planes, etc.).
    // Per-material cull needs dual pipelines or dynamic cull — later.
    rasterizer.cullMode = VK_CULL_MODE_NONE;
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

    // Opaque: no blend needed (shader forces a=1). Transparent pipeline blends.
    VkPipelineColorBlendAttachmentState blend_off = {};
    blend_off.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_off.blendEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend_on = blend_off;
    blend_on.blendEnable = VK_TRUE;
    blend_on.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_on.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_on.colorBlendOp = VK_BLEND_OP_ADD;
    blend_on.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_on.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_on.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &blend_off;

    std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount =
        static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkPipelineDepthStencilStateCreateInfo depth_opaque{};
    depth_opaque.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_opaque.depthTestEnable = VK_TRUE;
    depth_opaque.depthWriteEnable = VK_TRUE;
    depth_opaque.depthCompareOp = gfx::kDepthCompare;

    // Transparent: test against opaque depth, do NOT write (so cabin stays visible
    // through glass regardless of draw order among transparent batches).
    VkPipelineDepthStencilStateCreateInfo depth_transparent = depth_opaque;
    depth_transparent.depthWriteEnable = VK_FALSE;

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_opaque;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = renderer.vk.pipeline_layout;
    pipeline_info.renderPass = renderer.main_pass.render_pass;
    pipeline_info.subpass = 0;

    if (vkCreateGraphicsPipelines(renderer.vk.device, VK_NULL_HANDLE, 1,
                                  &pipeline_info, nullptr,
                                  &renderer.vk.pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(renderer.vk.device, vertex_shader_module, nullptr);
        vkDestroyShaderModule(renderer.vk.device, fragment_shader_module, nullptr);
        LOG_ERROR("Failed to create opaque graphics pipeline");
        return false;
    }

    color_blending.pAttachments = &blend_on;
    pipeline_info.pDepthStencilState = &depth_transparent;
    if (vkCreateGraphicsPipelines(renderer.vk.device, VK_NULL_HANDLE, 1,
                                  &pipeline_info, nullptr,
                                  &renderer.vk.transparent_pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(renderer.vk.device, vertex_shader_module, nullptr);
        vkDestroyShaderModule(renderer.vk.device, fragment_shader_module, nullptr);
        LOG_ERROR("Failed to create transparent graphics pipeline");
        return false;
    }

    vkDestroyShaderModule(renderer.vk.device, vertex_shader_module, nullptr);
    vkDestroyShaderModule(renderer.vk.device, fragment_shader_module, nullptr);
    LOG_INFO("[Pipeline] Opaque + transparent (depth-write-off) shade pipelines ready");
    return true;
}

bool gfx::Engine::init_depth_prepass_pipeline() {
    std::vector<unsigned int> vertex_code;
    std::vector<unsigned int> fragment_code;
    if (!load_shader_source("shaders/pbr.vert.spv", vertex_code)) {
        LOG_ERROR("Failed to load vertex shader for depth prepass");
        return false;
    }
    // Fragment stage: MASK alpha test + skip BLEND (no solid depth for cutouts).
    if (!load_shader_source("shaders/depth_prepass.frag.spv", fragment_code)) {
        LOG_ERROR("Failed to load depth_prepass.frag");
        return false;
    }

    VkShaderModule vertex_shader_module = VK_NULL_HANDLE;
    VkShaderModule fragment_shader_module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo vertex_shader_info{};
    vertex_shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_shader_info.codeSize = vertex_code.size() * sizeof(unsigned int);
    vertex_shader_info.pCode = vertex_code.data();
    if (vkCreateShaderModule(renderer.vk.device, &vertex_shader_info, nullptr,
                             &vertex_shader_module) != VK_SUCCESS) {
        LOG_ERROR("Failed to create depth-prepass vertex module");
        return false;
    }
    VkShaderModuleCreateInfo fragment_shader_info{};
    fragment_shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_shader_info.codeSize = fragment_code.size() * sizeof(unsigned int);
    fragment_shader_info.pCode = fragment_code.data();
    if (vkCreateShaderModule(renderer.vk.device, &fragment_shader_info, nullptr,
                             &fragment_shader_module) != VK_SUCCESS) {
        vkDestroyShaderModule(renderer.vk.device, vertex_shader_module, nullptr);
        LOG_ERROR("Failed to create depth-prepass fragment module");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_shader_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_shader_module;
    stages[1].pName = "main";

    // Same vertex layout as PBR (skin + color attrs for prepass MASK alpha).
    static const VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = static_cast<uint32_t>(gfx::Vertex::stride),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};

    static const VkVertexInputAttributeDescription attr_descs[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 12},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 24},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R16G16B16A16_SFLOAT, .offset = 40},
        {.location = 4, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 48},
        {.location = 5, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 52},
        {.location = 6, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UINT, .offset = 56},
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_desc;
    vertex_input_info.vertexAttributeDescriptionCount = 7;
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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
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
    depth_stencil.depthCompareOp = gfx::kDepthCompare;

    std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
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
        vkDestroyShaderModule(renderer.vk.device, fragment_shader_module, nullptr);
        LOG_ERROR("Failed to create depth-prepass pipeline");
        return false;
    }

    vkDestroyShaderModule(renderer.vk.device, vertex_shader_module, nullptr);
    vkDestroyShaderModule(renderer.vk.device, fragment_shader_module, nullptr);
    return true;
}

bool gfx::Engine::init_shadow_pipeline() {
    if (!renderer.shadow_map.is_ready())
        return true;

    std::vector<unsigned int> vertex_code;
    std::vector<unsigned int> fragment_code;
    if (!load_shader_source("shaders/pbr.vert.spv", vertex_code) ||
        !load_shader_source("shaders/depth_prepass.frag.spv", fragment_code)) {
        LOG_ERROR("[Shadow] failed to load depth shaders");
        return false;
    }

    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = vertex_code.size() * sizeof(unsigned int);
    ci.pCode = vertex_code.data();
    if (vkCreateShaderModule(renderer.vk.device, &ci, nullptr, &vert) != VK_SUCCESS)
        return false;
    ci.codeSize = fragment_code.size() * sizeof(unsigned int);
    ci.pCode = fragment_code.data();
    if (vkCreateShaderModule(renderer.vk.device, &ci, nullptr, &frag) != VK_SUCCESS) {
        vkDestroyShaderModule(renderer.vk.device, vert, nullptr);
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

    static const VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = static_cast<uint32_t>(gfx::Vertex::stride),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    static const VkVertexInputAttributeDescription attr_descs[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 12},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 24},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R16G16B16A16_SFLOAT, .offset = 40},
        {.location = 4, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 48},
        {.location = 5, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 52},
        {.location = 6, .binding = 0, .format = VK_FORMAT_R8G8B8A8_UINT, .offset = 56},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 1;
    vertex_input_info.pVertexBindingDescriptions = &binding_desc;
    vertex_input_info.vertexAttributeDescriptionCount = 7;
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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = -1.25f;
    rasterizer.depthBiasSlopeFactor = -1.5f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = gfx::kDepthCompare;

    std::array<VkDynamicState, 2> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT,
                                                    VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = renderer.vk.pipeline_layout;
    pipeline_info.renderPass = renderer.shadow_map.render_pass();
    pipeline_info.subpass = 0;

    const VkResult r = vkCreateGraphicsPipelines(
        renderer.vk.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
        &renderer.vk.shadow_pipeline);
    vkDestroyShaderModule(renderer.vk.device, vert, nullptr);
    vkDestroyShaderModule(renderer.vk.device, frag, nullptr);
    if (r != VK_SUCCESS) {
        LOG_ERROR("[Shadow] pipeline create failed");
        return false;
    }
    return true;
}