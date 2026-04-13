#pragma once

namespace core{
struct GameObject
{
    uint32_t root_transform_index;   // main transform for the whole entity
    uint32_t skin_index = ~0u;      // shared skeleton if any
    // game logic, health, AI, etc.
};
}; // namespace core