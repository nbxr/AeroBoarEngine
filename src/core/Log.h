#pragma once

#include <iostream>

namespace core {

// Lightweight logging macros (centralized in core/ per project hygiene rules).
// These preserve the original streaming convenience: LOG_ERROR("msg: " << value);
// All call sites continue to use the bare LOG_ERROR / LOG_INFO names.

#define LOG_ERROR(value) std::cerr << value << std::endl
#define LOG_INFO(value)  std::cout << value << std::endl

} // namespace core
