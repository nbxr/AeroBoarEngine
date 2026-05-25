# Product Context: AeroBoarEngine

## Problem Statement
Rendering high-fidelity VR content on mobile hardware like the Meta Quest 3 is challenging due to limited thermal and comsputational budgets. Standard rendering pipelines often suffer from excessive DRAM traffic and CPU overhead.

## User Goals
- Developers need a rendering engine that provides a high-performance baseline for VR applications.
- Minimize latency and maximize frame rate stability on mobile VR hardware.
- Reduce the complexity of managing advanced Vulkan features like subpasses and bindless descriptors.

## Target Audience
- VR developers targeting Meta Quest 3 and similar mobile VR platforms.
- Graphics engineers interested in highly optimized Vulkan implementations.

## Key UX/Performance Goals
- Extremely low motion-to-photon latency.
- Efficient use of GPU on-chip memory (GMEM).
- Low CPU overhead through GPU-driven rendering techniques.