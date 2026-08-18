#pragma once

#include <vulkan/vulkan.h>

namespace gfx {

// Reverse-Z: clip depth 1 = near, 0 = far (still Vulkan [0, 1]).
// Better far-field precision at large far/near ratios (Quest / outdoor).
constexpr float kDepthClear = 0.0f;
constexpr VkCompareOp kDepthCompare = VK_COMPARE_OP_GREATER;
constexpr VkCompareOp kDepthCompareLequal = VK_COMPARE_OP_GREATER_OR_EQUAL;

} // namespace gfx
