#pragma once
#include <cstdint>

namespace core {

#define INVALID_HANDLE UINT32_MAX

template <typename T> struct Handle {
  public:
    uint32_t value;

    operator uint32_t() const { return value; }
    uint32_t operator*() const { return value; }
    explicit Handle(uint32_t h) : value(h) {}
    Handle() : value(INVALID_HANDLE) {}
};

} // namespace core
