#pragma once

// Tracy client. Enable with:
//   cmake --preset windows-vs2026 -DAERO_TRACY=ON
//   cmake --build --preset windows-debug
// GUI: Tracy v0.14.1 from https://github.com/wolfpld/tracy/releases (port 8086).
// TRACY_ON_DEMAND: the game does not wait for a connection.
//
// CPU: FrameStats::CpuScope emits zones (cpu.input / simulate / ...).
// GPU: gfx::GpuTimestamps also emits Tracy Vulkan zones when AERO_TRACY=ON.
// FrameMark is issued once per main-loop iteration.

#include <tracy/Tracy.hpp>
#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>
#endif

#ifndef TRACY_ENABLE
#define AERO_ZONE_SCOPED
#define AERO_ZONE_NAMED(name)
#else
#define AERO_ZONE_SCOPED ZoneScoped
#define AERO_ZONE_NAMED(name) ZoneScopedN(name)
#endif
