#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>

namespace core {

// Lightweight logging macros (centralized in core/ per project hygiene rules).
// These preserve the original streaming convenience: LOG_ERROR("msg: " << value);
// All call sites continue to use the bare LOG_ERROR / LOG_INFO names.
//
// Enhanced: messages are also appended to a log file (if initialized) with flush
// so that Vulkan validation errors survive process crashes / device lost.

#define LOG_ERROR(value)                                        \
    do {                                                        \
        std::ostringstream oss;                                 \
        oss << value;                                           \
        std::string s = oss.str();                              \
        std::cerr << s << std::endl;                            \
        core::write_to_log_file(s);                             \
    } while (0)

#define LOG_INFO(value)                                         \
    do {                                                        \
        std::ostringstream oss;                                 \
        oss << value;                                           \
        std::string s = oss.str();                              \
        std::cout << s << std::endl;                            \
        core::write_to_log_file(s);                             \
    } while (0)

// Optional file sink for logs (and specifically for Vulkan validation layer
// debug messages). Call early (e.g. start of main) to enable. The file is
// opened in append mode and every write is flushed to maximize survival
// across crashes.
//
// The log is always created relative to the process's *current working directory*
// at the time init_file_log() is called (this matches the VS debugger working
// directory configured in CMakeLists.txt to ${CMAKE_BINARY_DIR}, i.e. the build
// folder when running under the debugger).
inline std::ofstream g_log_file;

inline std::string init_file_log(const std::string& filename = "aero_boar.log") {
    namespace fs = std::filesystem;

    // Resolve against the actual current working directory so behavior is
    // predictable and matches the project's configured debugger cwd.
    fs::path log_path = fs::absolute(fs::current_path() / fs::path(filename));

    g_log_file.open(log_path, std::ios::out | std::ios::app);

    std::string path_str = log_path.string();

    if (g_log_file.is_open()) {
        g_log_file << "=== AeroBoar log started ===" << std::endl;
        g_log_file << "Log file: " << path_str << std::endl;
        g_log_file << "Working directory at startup: " << fs::current_path().string() << std::endl;
        g_log_file.flush();

        // Announce the location directly to the user's terminal.
        // (We deliberately avoid OutputDebugStringA here to keep <windows.h> out of this header.)
        std::string announcement = "[Log] Writing diagnostics + Vulkan validation to: " + path_str;
        std::cout << announcement << std::endl;
        std::cout.flush();
    } else {
        std::cerr << "[Log] WARNING: Failed to open log file at " << path_str << std::endl;
    }

    return path_str;
}

inline void close_file_log() {
    if (g_log_file.is_open()) {
        g_log_file << "=== AeroBoar log closed ===" << std::endl;
        g_log_file.flush();
        g_log_file.close();
    }
}

inline void write_to_log_file(const std::string& message) {
    if (g_log_file.is_open()) {
        g_log_file << message << std::endl;
        g_log_file.flush();
    }
}

} // namespace core
