#pragma once

#include "ecs/Entity.h"

namespace ecs {

// Phase 4 will expand to fixed-union payloads (ecs-plan §6.1).
// Stub so IScript::on_event can compile now.
enum class EventType : uint32_t {
    None = 0,
};

struct Event {
    EventType type = EventType::None;
    Entity source = kInvalidEntity;
    Entity target = kInvalidEntity;
    float time = 0.0f;
};

} // namespace ecs
