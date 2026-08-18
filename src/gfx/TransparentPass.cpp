#include "gfx/TransparentPass.h"
#include "gfx/Depth.h"
#include "gfx/GpuCulling.h"
#include "gfx/Material.h"
#include "gfx/MaterialManager.h"
#include "gfx/MeshManager.h"
#include "gfx/PbrPush.h"
#include "gfx/Renderer.h"
#include "gfx/ShaderLoader.h"
#include "core/Frustum.h"
#include "core/Log.h"
#include "scene/SceneManager.h"

#include <algorithm>
#include <array>

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

void destroy_image(VkDevice device, VmaAllocator allocator, AllocatedImage& img) {
    if (img.view != VK_NULL_HANDLE)
        vkDestroyImageView(device, img.view, nullptr);
    if (img.handle != VK_NULL_HANDLE)
        vmaDestroyImage(allocator, img.handle, img.allocation);
    img = {};
}

core::AABB world_aabb(const glm::mat4& m, const core::AABB& local) {
    if (!local.is_valid())
        return {};
    core::AABB out;
    out.min = glm::vec3(1e30f);
    out.max = glm::vec3(-1e30f);
    const glm::vec3& bmin = local.min;
    const glm::vec3& bmax = local.max;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c((i & 1) ? bmax.x : bmin.x, (i & 2) ? bmax.y : bmin.y,
                          (i & 4) ? bmax.z : bmin.z);
        const glm::vec3 w = glm::vec3(m * glm::vec4(c, 1.0f));
        out.min = glm::min(out.min, w);
        out.max = glm::max(out.max, w);
    }
    return out;
}

void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                   VkImageLayout src, VkImageLayout dst, VkAccessFlags src_a,
                   VkAccessFlags dst_a, VkPipelineStageFlags src_s,
                   VkPipelineStageFlags dst_s) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.srcAccessMask = src_a;
    b.dstAccessMask = dst_a;
    b.oldLayout = src;
    b.newLayout = dst;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, src_s, dst_s, 0, 0, nullptr, 0, nullptr, 1, &b);
}

} // namespace

bool TransparentPass::create_color_image(VkDevice device, VmaAllocator allocator,
                                         VkExtent2D extent, VkFormat format,
                                         AllocatedImage& out) {
    VkImageCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    if (vmaCreateImage(allocator, &info, &alloc, &out.handle, &out.allocation,
                       &out.info) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = out.handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device, &view, nullptr, &out.view) != VK_SUCCESS) {
        vmaDestroyImage(allocator, out.handle, out.allocation);
        out = {};
        return false;
    }
    return true;
}

bool TransparentPass::create(VkDevice device, VmaAllocator allocator,
                             Renderer& renderer) {
    destroy(device, allocator);
    device_ = device;

    if (!create_render_passes(device, renderer.vk.swap_chain_image_format,
                              renderer.vk.depth_format))
        return false;
    if (!create_pipelines(device, renderer.vk.pipeline_layout))
        return false;
    if (!create_composite_descriptors(device))
        return false;
    if (!resize(device, allocator, renderer))
        return false;

    ready_ = true;
    LOG_INFO("[Transparent] CPU sort + weighted blended OIT ready");
    return true;
}

void TransparentPass::destroy(VkDevice device, VmaAllocator allocator) {
    destroy_framebuffers(device);
    destroy_images(device, allocator);

    if (composite_pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device, composite_pipeline_, nullptr);
    if (gather_pipeline_ != VK_NULL_HANDLE)
        vkDestroyPipeline(device, gather_pipeline_, nullptr);
    if (composite_layout_ != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, composite_layout_, nullptr);
    if (composite_set_layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, composite_set_layout_, nullptr);
    if (composite_pool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, composite_pool_, nullptr);
    if (sampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device, sampler_, nullptr);
    if (gather_rp_ != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, gather_rp_, nullptr);
    if (composite_rp_ != VK_NULL_HANDLE)
        vkDestroyRenderPass(device, composite_rp_, nullptr);

    gather_pipeline_ = VK_NULL_HANDLE;
    composite_pipeline_ = VK_NULL_HANDLE;
    composite_layout_ = VK_NULL_HANDLE;
    composite_set_layout_ = VK_NULL_HANDLE;
    composite_pool_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    gather_rp_ = VK_NULL_HANDLE;
    composite_rp_ = VK_NULL_HANDLE;
    composite_sets_ = {};
    ready_ = wboit_ready_ = false;
    items_.clear();
}

void TransparentPass::destroy_images(VkDevice device, VmaAllocator allocator) {
    for (auto& img : accum_)
        destroy_image(device, allocator, img);
    for (auto& img : reveal_)
        destroy_image(device, allocator, img);
}

void TransparentPass::destroy_framebuffers(VkDevice device) {
    for (auto fb : gather_fbs_) {
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, fb, nullptr);
    }
    for (auto fb : composite_fbs_) {
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, fb, nullptr);
    }
    gather_fbs_.clear();
    composite_fbs_.clear();
    swap_count_ = 0;
}

bool TransparentPass::create_images(VkDevice device, VmaAllocator allocator,
                                    VkExtent2D extent) {
    for (uint32_t i = 0; i < kMaxFrames; ++i) {
        if (!create_color_image(device, allocator, extent,
                                VK_FORMAT_R16G16B16A16_SFLOAT, accum_[i]) ||
            !create_color_image(device, allocator, extent, VK_FORMAT_R16_SFLOAT,
                                reveal_[i])) {
            LOG_ERROR("[Transparent] Failed to create WBOIT targets");
            return false;
        }
    }
    return true;
}

bool TransparentPass::create_render_passes(VkDevice device, VkFormat swap_format,
                                           VkFormat depth_format) {
    // Gather: accum + reveal + loaded 1x opaque depth.
    VkAttachmentDescription2 atts[3]{};
    atts[0].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    atts[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    atts[1] = atts[0];
    atts[1].format = VK_FORMAT_R16_SFLOAT;

    atts[2].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    atts[2].format = depth_format;
    atts[2].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    // STORE so a later gather LOAD is well-defined. DONT_CARE here made
    // BestPractices-StoreOpDontCareThenLoadOpLoad fire on the next use of
    // this 1× depth (validation tracks the last attachment store, not the
    // intervening MSAA resolve).
    atts[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[2].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    atts[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference2 color_refs[2]{};
    color_refs[0].sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    color_refs[0].attachment = 0;
    color_refs[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_refs[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    color_refs[1] = color_refs[0];
    color_refs[1].attachment = 1;

    VkAttachmentReference2 depth_ref{};
    depth_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    depth_ref.attachment = 2;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_ref.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    VkSubpassDescription2 sub{};
    sub.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 2;
    sub.pColorAttachments = color_refs;
    sub.pDepthStencilAttachment = &depth_ref;

    VkSubpassDependency2 gather_in{};
    gather_in.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    gather_in.srcSubpass = VK_SUBPASS_EXTERNAL;
    gather_in.dstSubpass = 0;
    gather_in.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    gather_in.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                              VK_ACCESS_SHADER_READ_BIT;
    gather_in.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    gather_in.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency2 gather_out{};
    gather_out.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    gather_out.srcSubpass = 0;
    gather_out.dstSubpass = VK_SUBPASS_EXTERNAL;
    gather_out.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    gather_out.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    gather_out.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    gather_out.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkSubpassDependency2 gather_deps[] = {gather_in, gather_out};

    VkRenderPassCreateInfo2 rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    rp.attachmentCount = 3;
    rp.pAttachments = atts;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 2;
    rp.pDependencies = gather_deps;
    if (vkCreateRenderPass2(device, &rp, nullptr, &gather_rp_) != VK_SUCCESS) {
        LOG_ERROR("[Transparent] gather render pass failed");
        return false;
    }

    VkAttachmentDescription2 swap{};
    swap.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
    swap.format = swap_format;
    swap.samples = VK_SAMPLE_COUNT_1_BIT;
    swap.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    swap.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    swap.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    swap.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    swap.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swap.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference2 swap_ref{};
    swap_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
    swap_ref.attachment = 0;
    swap_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swap_ref.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkSubpassDescription2 csub{};
    csub.sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
    csub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    csub.colorAttachmentCount = 1;
    csub.pColorAttachments = &swap_ref;

    VkSubpassDependency2 comp_in{};
    comp_in.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    comp_in.srcSubpass = VK_SUBPASS_EXTERNAL;
    comp_in.dstSubpass = 0;
    comp_in.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    comp_in.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_SHADER_READ_BIT;
    comp_in.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    comp_in.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_SHADER_READ_BIT;

    VkSubpassDependency2 comp_out{};
    comp_out.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
    comp_out.srcSubpass = 0;
    comp_out.dstSubpass = VK_SUBPASS_EXTERNAL;
    comp_out.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    comp_out.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    comp_out.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    comp_out.dstAccessMask = 0;

    VkSubpassDependency2 comp_deps[] = {comp_in, comp_out};

    VkRenderPassCreateInfo2 crp{};
    crp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    crp.attachmentCount = 1;
    crp.pAttachments = &swap;
    crp.subpassCount = 1;
    crp.pSubpasses = &csub;
    crp.dependencyCount = 2;
    crp.pDependencies = comp_deps;
    if (vkCreateRenderPass2(device, &crp, nullptr, &composite_rp_) != VK_SUCCESS) {
        LOG_ERROR("[Transparent] composite render pass failed");
        return false;
    }
    return true;
}

bool TransparentPass::create_pipelines(VkDevice device, VkPipelineLayout pbr_layout) {
    VkShaderModule vs{}, wboit_fs{}, fsvs{}, comp_fs{};
    if (!load_module(device, "shaders/pbr.vert.spv", vs) ||
        !load_module(device, "shaders/pbr_wboit.frag.spv", wboit_fs)) {
        LOG_ERROR("[Transparent] missing pbr / pbr_wboit SPIR-V");
        if (vs)
            vkDestroyShaderModule(device, vs, nullptr);
        if (wboit_fs)
            vkDestroyShaderModule(device, wboit_fs, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = wboit_fs;
    stages[1].pName = "main";

    static const VkVertexInputBindingDescription binding = {
        0, static_cast<uint32_t>(64), VK_VERTEX_INPUT_RATE_VERTEX};
    static const VkVertexInputAttributeDescription attrs[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 24},
        {3, 0, VK_FORMAT_R16G16B16A16_SFLOAT, 40},
        {4, 0, VK_FORMAT_R8G8B8A8_UNORM, 48},
        {5, 0, VK_FORMAT_R8G8B8A8_UNORM, 52},
        {6, 0, VK_FORMAT_R8G8B8A8_UINT, 56},
    };
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 7;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1.0f;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    // GREATER_OR_EQUAL so coplanar glass still tests against opaque z.
    ds.depthCompareOp = kDepthCompareLequal;

    VkPipelineColorBlendAttachmentState blend[2]{};
    // accum: ONE, ONE
    blend[0].blendEnable = VK_TRUE;
    blend[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend[0].colorBlendOp = VK_BLEND_OP_ADD;
    blend[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend[0].alphaBlendOp = VK_BLEND_OP_ADD;
    blend[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    // reveal: ZERO, ONE_MINUS_SRC_COLOR  (dst *= 1-α)
    blend[1] = blend[0];
    blend[1].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    blend[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 2;
    cb.pAttachments = blend;

    std::array<VkDynamicState, 2> dyn_states = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states.data();

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
    gp.layout = pbr_layout;
    gp.renderPass = gather_rp_;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr,
                                  &gather_pipeline_) != VK_SUCCESS) {
        LOG_ERROR("[Transparent] gather pipeline failed");
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, wboit_fs, nullptr);
        return false;
    }
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, wboit_fs, nullptr);

    if (!load_module(device, "shaders/fullscreen.vert.spv", fsvs) ||
        !load_module(device, "shaders/wboit_composite.frag.spv", comp_fs)) {
        LOG_ERROR("[Transparent] missing composite SPIR-V");
        if (fsvs)
            vkDestroyShaderModule(device, fsvs, nullptr);
        if (comp_fs)
            vkDestroyShaderModule(device, comp_fs, nullptr);
        return false;
    }

    VkDescriptorSetLayoutBinding binds[2]{};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[1] = binds[0];
    binds[1].binding = 1;
    VkDescriptorSetLayoutCreateInfo sl{};
    sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sl.bindingCount = 2;
    sl.pBindings = binds;
    if (vkCreateDescriptorSetLayout(device, &sl, nullptr, &composite_set_layout_) !=
        VK_SUCCESS)
        return false;

    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &composite_set_layout_;
    if (vkCreatePipelineLayout(device, &pl, nullptr, &composite_layout_) !=
        VK_SUCCESS)
        return false;

    stages[0].module = fsvs;
    stages[1].module = comp_fs;
    VkPipelineVertexInputStateCreateInfo vi_empty{};
    vi_empty.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    VkPipelineColorBlendAttachmentState cblend{};
    cblend.blendEnable = VK_TRUE;
    cblend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cblend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cblend.colorBlendOp = VK_BLEND_OP_ADD;
    cblend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cblend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cblend.alphaBlendOp = VK_BLEND_OP_ADD;
    cblend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cb.attachmentCount = 1;
    cb.pAttachments = &cblend;
    gp.pVertexInputState = &vi_empty;
    gp.layout = composite_layout_;
    gp.renderPass = composite_rp_;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr,
                                  &composite_pipeline_) != VK_SUCCESS) {
        LOG_ERROR("[Transparent] composite pipeline failed");
        vkDestroyShaderModule(device, fsvs, nullptr);
        vkDestroyShaderModule(device, comp_fs, nullptr);
        return false;
    }
    vkDestroyShaderModule(device, fsvs, nullptr);
    vkDestroyShaderModule(device, comp_fs, nullptr);
    return true;
}

bool TransparentPass::create_composite_descriptors(VkDevice device) {
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &si, nullptr, &sampler_) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = kMaxFrames * 2;
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = kMaxFrames;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(device, &pi, nullptr, &composite_pool_) != VK_SUCCESS)
        return false;

    std::array<VkDescriptorSetLayout, kMaxFrames> layouts{};
    layouts.fill(composite_set_layout_);
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = composite_pool_;
    ai.descriptorSetCount = kMaxFrames;
    ai.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &ai, composite_sets_.data()) != VK_SUCCESS)
        return false;
    return true;
}

bool TransparentPass::create_framebuffers(VkDevice device, Renderer& renderer) {
    destroy_framebuffers(device);
    const uint32_t n = renderer.vk.swap_chain_image_count;
    swap_count_ = n;
    gather_fbs_.assign(static_cast<size_t>(n) * kMaxFrames, VK_NULL_HANDLE);
    composite_fbs_.assign(n, VK_NULL_HANDLE);

    const uint32_t w = renderer.vk.swap_chain_extent.width;
    const uint32_t h = renderer.vk.swap_chain_extent.height;

    for (uint32_t img = 0; img < n; ++img) {
        VkImageView depth = renderer.main_pass.uses_depth_resolve
                                ? renderer.main_pass.resolved_depth_images[img].view
                                : renderer.main_pass.depth_images[img].view;
        for (uint32_t f = 0; f < kMaxFrames; ++f) {
            VkImageView atts[3] = {accum_[f].view, reveal_[f].view, depth};
            VkFramebufferCreateInfo fi{};
            fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fi.renderPass = gather_rp_;
            fi.attachmentCount = 3;
            fi.pAttachments = atts;
            fi.width = w;
            fi.height = h;
            fi.layers = 1;
            if (vkCreateFramebuffer(device, &fi, nullptr,
                                    &gather_fbs_[img * kMaxFrames + f]) !=
                VK_SUCCESS)
                return false;
        }

        VkImageView swap = renderer.vk.swap_chain_image_views[img];
        VkFramebufferCreateInfo cfi{};
        cfi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        cfi.renderPass = composite_rp_;
        cfi.attachmentCount = 1;
        cfi.pAttachments = &swap;
        cfi.width = w;
        cfi.height = h;
        cfi.layers = 1;
        if (vkCreateFramebuffer(device, &cfi, nullptr, &composite_fbs_[img]) !=
            VK_SUCCESS)
            return false;
    }

    for (uint32_t f = 0; f < kMaxFrames; ++f) {
        VkDescriptorImageInfo ia{};
        ia.sampler = sampler_;
        ia.imageView = accum_[f].view;
        ia.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo ir = ia;
        ir.imageView = reveal_[f].view;
        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = composite_sets_[f];
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &ia;
        writes[1] = writes[0];
        writes[1].dstBinding = 1;
        writes[1].pImageInfo = &ir;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }
    return true;
}

void TransparentPass::release_swapchain_views(VkDevice device) {
    destroy_framebuffers(device);
}

bool TransparentPass::resize(VkDevice device, VmaAllocator allocator,
                             Renderer& renderer) {
    destroy_framebuffers(device);
    destroy_images(device, allocator);
    wboit_ready_ = false;
    if (!create_images(device, allocator, renderer.vk.swap_chain_extent))
        return false;
    if (!create_framebuffers(device, renderer))
        return false;
    wboit_ready_ = gather_pipeline_ != VK_NULL_HANDLE &&
                   composite_pipeline_ != VK_NULL_HANDLE;
    return true;
}

void TransparentPass::collect_and_sort(const scene::SceneManager& scene,
                                       const MaterialManager& materials,
                                       const MeshManager& meshes,
                                       const core::Frustum& frustum,
                                       const glm::vec3& camera_pos) {
    items_.clear();
    const uint32_t n = scene.render_mesh_count();
    items_.reserve(n);
    const auto& xforms = scene.transforms();

    for (uint32_t i = 0; i < n; ++i) {
        const auto& rm = scene.get_render_mesh(i);
        const uint32_t flags = materials.get_material_flags(rm.material_index);
        if ((flags & (Material::kFlagAlphaBlend | Material::kFlagTransmission)) == 0)
            continue;
        if (rm.transform_index == ~0u || !xforms.is_alive(rm.transform_index))
            continue;

        const glm::mat4& world = xforms.get_world_matrix(rm.transform_index);
        core::AABB aabb = world_aabb(world, rm.local_aabb);
        if (rm.skin_index != ~0u && aabb.is_valid()) {
            const glm::vec3 c = aabb.center();
            const glm::vec3 e = aabb.extents() * 1.25f;
            aabb.min = c - e;
            aabb.max = c + e;
        }
        if (!frustum.intersects_aabb(aabb))
            continue;

        DrawItem d{};
        const glm::vec3 center = aabb.is_valid() ? aabb.center() : glm::vec3(world[3]);
        d.sort_key = glm::dot(center - camera_pos, center - camera_pos);
        d.instance.model = world;
        uint32_t joint_base = 0, joint_count = 0;
        if (rm.skin_index != ~0u && rm.skin_index < scene.skins().skin_count()) {
            const auto& sk = scene.skins().skin(rm.skin_index);
            joint_base = sk.palette_offset;
            joint_count = static_cast<uint32_t>(sk.joint_transform_indices.size());
        }
        d.instance.meta = glm::uvec4(rm.material_index, joint_base, joint_count, 0);
        d.index_count = meshes.get_primitive_index_count(rm.mesh_index);
        d.index_offset = meshes.get_primitive_index_offset(rm.mesh_index);
        d.vertex_offset =
            static_cast<int32_t>(meshes.get_primitive_vertex_offset(rm.mesh_index));
        if (d.index_count == 0)
            continue;
        items_.push_back(d);
    }

    std::sort(items_.begin(), items_.end(),
              [](const DrawItem& a, const DrawItem& b) {
                  return a.sort_key > b.sort_key; // far first
              });
}

void TransparentPass::upload_instances(GpuCulling& culling, uint32_t frame_index) {
    if (items_.empty() || !culling.is_ready() || !culling.has_transparent_half())
        return;
    const uint32_t base = culling.instance_slot_count();
    const uint32_t cap = base;
    const uint32_t n = std::min(static_cast<uint32_t>(items_.size()), cap);
    auto& buf = culling.out_instances(frame_index);
    if (!buf.mapped_data)
        return;
    auto* dst = static_cast<DrawInstanceGPU*>(buf.mapped_data) + base;
    for (uint32_t i = 0; i < n; ++i)
        dst[i] = items_[i].instance;
}

void TransparentPass::record_sorted_draws(VkCommandBuffer cmd, Renderer& renderer,
                                          VkPipeline pipeline,
                                          const glm::mat4& view_proj,
                                          uint32_t instance_base) const {
    if (items_.empty() || pipeline == VK_NULL_HANDLE)
        return;
    auto& vk = renderer.vk;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout,
                            0, 1, &vk.bindless_descriptor_sets[renderer.current_frame],
                            0, nullptr);

    VkViewport viewport{};
    viewport.width = static_cast<float>(vk.swap_chain_extent.width);
    viewport.height = static_cast<float>(vk.swap_chain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = vk.swap_chain_extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    auto& index_buf = renderer.mesh_manager.get_render_index_buffer();
    vkCmdBindIndexBuffer(cmd, index_buf.buffer, 0, VK_INDEX_TYPE_UINT32);
    auto& vertex_buf = renderer.mesh_manager.get_render_vertex_buffer();
    VkDeviceSize vbo_off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buf.buffer, &vbo_off);

    PbrPush push{};
    push.viewProj = view_proj;
    vkCmdPushConstants(cmd, vk.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PbrPush), &push);

    const uint32_t cap = renderer.gpu_culling.instance_slot_count();
    const uint32_t n = std::min(static_cast<uint32_t>(items_.size()), cap);
    for (uint32_t i = 0; i < n; ++i) {
        const auto& d = items_[i];
        vkCmdDrawIndexed(cmd, d.index_count, 1, d.index_offset, d.vertex_offset,
                         instance_base + i);
    }
}

void TransparentPass::record_wboit(VkCommandBuffer cmd, Renderer& renderer,
                                   uint32_t frame_index, uint32_t image_index,
                                   const glm::mat4& view_proj, bool gpu_emit) {
    if (!wboit_ready_ || image_index >= swap_count_)
        return;
    if (!gpu_emit && items_.empty())
        return;

    const AllocatedImage& depth_img =
        renderer.main_pass.uses_depth_resolve
            ? renderer.main_pass.resolved_depth_images[image_index]
            : renderer.main_pass.depth_images[image_index];

    // Main RP already transitioned this image to SHADER_READ_ONLY (dep_out
    // publishes COMPUTE / SHADER_READ). A layout barrier must wait on that
    // access — DEPTH_WRITE against SHADER_READ_ONLY is a WAW with EndRenderPass
    // and trips BestPractices-ImageBarrierAccessLayout.
    image_barrier(cmd, depth_img.handle, VK_IMAGE_ASPECT_DEPTH_BIT,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_ACCESS_SHADER_READ_BIT,
                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);

    VkClearValue clears[3]{};
    clears[0].color = {{0.f, 0.f, 0.f, 0.f}};
    clears[1].color = {{1.f, 1.f, 1.f, 1.f}};
    clears[2].depthStencil = {kDepthClear, 0};

    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = gather_rp_;
    bi.framebuffer = gather_fbs_[image_index * kMaxFrames + frame_index];
    bi.renderArea.extent = renderer.vk.swap_chain_extent;
    bi.clearValueCount = 3;
    bi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);

    if (gpu_emit) {
        auto& vk = renderer.vk;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, gather_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout,
                                0, 1, &vk.bindless_descriptor_sets[renderer.current_frame],
                                0, nullptr);
        VkViewport vp{};
        vp.width = static_cast<float>(vk.swap_chain_extent.width);
        vp.height = static_cast<float>(vk.swap_chain_extent.height);
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{};
        sc.extent = vk.swap_chain_extent;
        vkCmdSetScissor(cmd, 0, 1, &sc);
        auto& index_buf = renderer.mesh_manager.get_render_index_buffer();
        vkCmdBindIndexBuffer(cmd, index_buf.buffer, 0, VK_INDEX_TYPE_UINT32);
        auto& vertex_buf = renderer.mesh_manager.get_render_vertex_buffer();
        VkDeviceSize vbo_off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buf.buffer, &vbo_off);
        PbrPush push{};
        push.viewProj = view_proj;
        vkCmdPushConstants(cmd, vk.pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PbrPush), &push);
        const uint32_t batches = renderer.gpu_culling.batch_count();
        auto& indirect =
            renderer.gpu_culling.indirect_cmds(frame_index, CullPass::Transparent);
        if (batches > 0)
            vkCmdDrawIndexedIndirect(cmd, indirect.buffer, 0, batches,
                                     sizeof(VkDrawIndexedIndirectCommand));
    } else {
        const uint32_t base = renderer.gpu_culling.instance_slot_count();
        record_sorted_draws(cmd, renderer, gather_pipeline_, view_proj, base);
    }

    vkCmdEndRenderPass(cmd);

    VkRenderPassBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    cbi.renderPass = composite_rp_;
    cbi.framebuffer = composite_fbs_[image_index];
    cbi.renderArea.extent = renderer.vk.swap_chain_extent;
    vkCmdBeginRenderPass(cmd, &cbi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, composite_layout_,
                            0, 1, &composite_sets_[frame_index], 0, nullptr);
    VkViewport viewport{};
    viewport.width = static_cast<float>(renderer.vk.swap_chain_extent.width);
    viewport.height = static_cast<float>(renderer.vk.swap_chain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = renderer.vk.swap_chain_extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

} // namespace gfx
