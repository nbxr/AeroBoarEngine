---
alwaysApply: false
---

# Engine Architecture

## Core Engine Components

### Rendering Engine
- Vulkan API
- OpenXR integration layer
- Scene rendering pipeline
- Vulkan memory management using VMA

### Input System
- OpenXR input handling
- Desktop controller mapping
- Haptic feedback support
- GLFW for windowing on desktop

### Audio System
- 3D spatial audio using OpenAL Soft
- OpenXR audio integration
- HRTF (Head-Related Transfer Function) support for realistic VR audio

### Physics Engine
- Integration with Jolt Physics library
- VR-specific collision handling

### Asset Pipeline
- glTF parsing using tinygltf
- PBR properties support

## Technical Requirements

### Core Engine Features
- OpenXR API integration for VR hardware support
- Cross-platform rendering pipeline using Vulkan
- Input handling for VR controllers and desktop input
- Asset management system
- Scene management and hierarchy
- Bindless rendering with PRB pipelines
- VK_EXT_descriptor_heap for efficient descriptor management
- Reverse-z depth buffer
- all GLM implementation must follow Vulkan best practices and conventions

### Desktop Development Mode
- Controller visualization in window
- Pinning controllers to visible positions
- Debugging tools for VR development
- Performance monitoring

### Meta Quest Support
- Compilation targeting Meta Quest hardware
- Quest-specific optimizations
- Oculus integration
- App packaging for Quest store