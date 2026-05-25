# Project Brief: AeroBoarEngine

## Overview
AeroBoarEngine is a high-performance, Vulkan-based rendering engine specifically optimized for VR hardware, with a primary focus on Meta Quest 3.

## Goals
- Achieve maximum performance on mobile VR (Quest 3) using advanced Vulkan features.
- Implement a highly optimized rendering pipeline (subpasses, multiview, GPU-driven).
- Maintain a clean, C-style architecture for performance and simplicity.
- Develop and validate the engine on Linux desktop using a pipeline designed for mobile constraints before targeting Quest 3 hardware.

## Requirements
- Strict adherence to Quest 3 optimization standards.
- Efficient memory management using VulkanMemoryAllocator (VMA).
- Strong support for compute-based culling and bindless rendering.
