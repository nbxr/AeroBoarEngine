#pragma once

// Tracy client hook. Linked via CMake (TracyClient). Enabled with
//   cmake -DAERO_TRACY=ON
// which sets TRACY_ENABLE on TracyClient. Instrumentation (ZoneScoped /
// FrameMark / Vulkan context) is not in the engine yet — include this header
// when adding it.
//
// GUI: download the matching Tracy release (v0.14.1) from
//   https://github.com/wolfpld/tracy/releases
// and connect to the running process (default port 8086). TRACY_ON_DEMAND is
// on so the game does not wait for a connection.

#include <tracy/Tracy.hpp>
