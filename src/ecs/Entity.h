#pragma once

#include <cstdint>

namespace ecs {

using Entity = uint32_t;
constexpr Entity kInvalidEntity = ~0u;

} // namespace ecs
