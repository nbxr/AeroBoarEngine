#pragma once

#include <vulkan/vulkan.h>
#include "VulkanContext.h"
#include "AllocatedBuffer.h"

namespace gfx {

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
    // Pipelines
    VkPipeline culling_pipeline{VK_NULL_HANDLE};
    VkPipeline animation_pipeline{VK_NULL_HANDLE};

    // Descriptor sets (can share the global bindless set, or have own)
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};

    // Per-frame resources    
    AllocatedBuffer indirect_draw_buffer{};
    AllocatedBuffer draw_count_buffer{};
    AllocatedBuffer animation_storage_buffer{};

    // timestamp queries or debug markers (optional, but useful for
    // profiling the compute pass) 
    VkQueryPool query_pool{VK_NULL_HANDLE};
    
    // Synchronization primitives for the compute pass
    VkSemaphore computeFinishedSemaphore = VK_NULL_HANDLE;
};

} // namespace gfx