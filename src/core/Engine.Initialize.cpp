#define VMA_IMPLEMENTATION

#include "AllocatedBuffer.h"
#include "AllocatedImage.h"
#include "Engine.h"
#include "Renderer.h"
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"

void core::Engine::initialize(core::Renderer &renderer) {
    init_vulkan(renderer);
    init_vma(renderer);
}

inline void init_vk_instance(core::Renderer &renderer,
                             vkb::InstanceBuilder &builder) {
    // vulkan instance
    auto inst_ret =
        builder.set_app_name("AeroBoar")
            .request_validation_layers(renderer.vk.enable_validation_layers)
            .use_default_debug_messenger()
            .build();

    if (!inst_ret) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    renderer.vk.instance = inst_ret.value();
}

inline void init_surface(core::Renderer &renderer) {
    // surface
    if (glfwCreateWindowSurface(renderer.vk.instance,
                                renderer.window.glfw_handle, nullptr,
                                &renderer.vk.surface) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan surface");
    }
}

inline void add_features(vkb::PhysicalDeviceSelector &selector) {
    // standard settings
    selector.set_minimum_version(1, 4).require_dedicated_transfer_queue();

    // enable bindless rendering
    selector.add_required_extension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

    VkPhysicalDeviceVulkan12Features features12 = {};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.descriptorIndexing = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    features12.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    features12.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    selector.set_required_features_12(features12);

    // enable descriptor heap
    // Enable VK_EXT_descriptor_indexing extension features
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT featuresIndexing = {};
    featuresIndexing.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
    featuresIndexing.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    featuresIndexing.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    featuresIndexing.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    featuresIndexing.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    featuresIndexing.runtimeDescriptorArray = VK_TRUE;
    featuresIndexing.descriptorBindingVariableDescriptorCount = VK_TRUE;
    featuresIndexing.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    selector.add_required_extension_features(featuresIndexing);
}

inline void init_physical_device(core::Renderer &renderer) {
    // Add
    vkb::PhysicalDeviceSelector selector{renderer.vk.instance};
    add_features(selector);
    auto phys_ret = selector.set_surface(renderer.vk.surface).select();

    if (!phys_ret) {
        throw std::runtime_error("Failed to find suitable physical device");
    }
    renderer.vk.physical_device = phys_ret.value();
}

inline void init_logical_device(core::Renderer &renderer) {
    vkb::DeviceBuilder device_builder{renderer.vk.physical_device};
    auto dev_ret = device_builder.build();
    if (!dev_ret) {
        throw std::runtime_error("Failed to find suitable logical device");
    }
    renderer.vk.device = dev_ret.value();
}

inline void init_graphics_queue(core::Renderer &renderer) {
    auto graphics_queue_ret =
        renderer.vk.device.get_queue(vkb::QueueType::graphics);
    if (!graphics_queue_ret) {
        throw std::runtime_error("Failed to find graphics queue");
    }
    VkQueue graphics_queue = graphics_queue_ret.value();
    renderer.vk.graphics_queue = graphics_queue;
}

inline void init_present_queue(core::Renderer &renderer) {
    auto present_queue_ret =
        renderer.vk.device.get_queue(vkb::QueueType::present);
    if (!present_queue_ret) {
        throw std::runtime_error("Failed to find present queue");
    }
    VkQueue present_queue = present_queue_ret.value();
    renderer.vk.present_queue = present_queue;
}

inline void init_transfer_queue(core::Renderer &renderer) {
    auto transfer_queue_ret =
        renderer.vk.device.get_queue(vkb::QueueType::transfer);
    if (!transfer_queue_ret) {
        // If no dedicated transfer queue, fall back to graphics queue
        transfer_queue_ret = renderer.vk.device.get_queue(vkb::QueueType::graphics);
        if (!transfer_queue_ret) {
            throw std::runtime_error("Failed to find transfer queue or graphics queue");
        }
    }
    VkQueue transfer_queue = transfer_queue_ret.value();
    renderer.vk.transfer_queue = transfer_queue;
}

inline void init_swapchain(core::Renderer &renderer) {
    vkb::SwapchainBuilder swapchain_builder{renderer.vk.device, renderer.vk.surface};
    auto swapchain_ret = swapchain_builder.build();
    
    if (!swapchain_ret) {
        throw std::runtime_error("Failed to create swapchain");
    }
    
    renderer.vk.swapchain = swapchain_ret.value();
}

inline void init_render_pass(core::Renderer &renderer) {
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = renderer.vk.swapchain.image_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    if (vkCreateRenderPass(renderer.vk.device, &render_pass_info, nullptr, &renderer.vk.render_pass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
}

void core::Engine::init_vulkan(core::Renderer &renderer) {
    // Initialize Vulkan using vk-bootstrap
    vkb::InstanceBuilder builder{};

    // vulkan instance
    init_vk_instance(renderer, builder);

    // surface
    init_surface(renderer);

    // physical and logical devices
    init_physical_device(renderer);
    init_logical_device(renderer);

    // queues
    init_graphics_queue(renderer);
    init_present_queue(renderer);
    init_transfer_queue(renderer);

    // swapchain
    init_swapchain(renderer);

    // render pass
    init_render_pass(renderer);
}

void core::Engine::init_vma(core::Renderer &renderer) {
    VmaAllocatorCreateInfo alloc_info = {};
    alloc_info.instance = renderer.vk.instance;
    alloc_info.physicalDevice = renderer.vk.physical_device;
    alloc_info.device = renderer.vk.device;
    alloc_info.vulkanApiVersion = VK_API_VERSION_1_4;

    if (vmaCreateAllocator(&alloc_info, &renderer.allocator) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan Memory Allocator");
    }
}

void core::Engine::init_renderer(Renderer &renderer) {
    
}