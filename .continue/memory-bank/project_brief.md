# Project Brief: AeroBoarEngine

## Overview
AeroBoarEngine is a high-performance, Vulkan-based rendering engine specifically optimized for VR hardware, with a primary focus on Meta Quest 3.

## Goals
- Achieve maximum performance on mobile VR (Quest 3) using advanced Vulkan features.
- Implement a highly optimized rendering pipeline (subpasses, multiview, GPU-driven).
- Maintain a clean, C-style architecture for performance and simplicity.
- Implement fully in a Linux desktop application using a pipeline optimized for mobile before targeting Quest 3 hardware.

## Requirements
- Strict adherence to Quest 3 optimization standards.
- Efficient memory management using VulkanMemoryAllocator.
- Support for compute-based culling and bindless rendering.
