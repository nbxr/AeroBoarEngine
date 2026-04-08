#pragma once

#include <vulkan/vulkan.h>
#include "VulkanContext.h"
#include "AllocatedBuffer.h"

namespace core {

/**
 * @brief Holds the resources required for the GPU-driven compute culling pass.
 * 
 * IMPORTANT: This context must be instantiated per frame in flight (e.g., within 
 * a PerFrameContext) to prevent race conditions where the CPU overwrites 
 * buffers currently being read by the GPU from a previous frame.
 * 
 * Following the architecture guide, this context manages the pipelines and 
 * buffers used to perform frustum and occlusion culling before the main 
 * rendering subpasses.
 */
struct ComputePassContext {
    VkPipeline cullingPipeline = VK_NULL_HANDLE;
    VkPipelineLayout cullingPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSet cullingDescriptorSet = VK_NULL_HANDLE;

    // Buffer containing the input draw commands (e.g., DrawIndexedIndirectCommand)
    AllocatedBuffer inputDrawBuffer;

    // Buffer containing the culled draw commands to be used by the graphics pass
    AllocatedBuffer indirectDrawBuffer;

    // Buffer for storing culling results or visibility metadata
    AllocatedBuffer visibilityBuffer;

    // Synchronization primitives for the compute pass
    VkSemaphore computeFinishedSemaphore = VK_NULL_HANDLE;
};

} // namespace core