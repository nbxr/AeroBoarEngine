# VR Game Engine Project Plan

## Project Overview
This project aims to develop a cross-platform game engine targeting VR via OpenXR, with desktop development mode and Meta Quest support.

## Project Goals
- Create a robust game engine using OpenXR for VR compatibility
- Implement desktop development mode with visible controller positioning
- Enable compilation for Meta Quest devices
- Support cross-platform development workflow

## Technical Requirements

### Core Engine Features
- OpenXR API integration for VR hardware support
- Cross-platform rendering pipeline using Vulkan
- Input handling for VR controllers and desktop input
- Asset management system
- Scene management and hierarchy

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

## Architecture Plan

### Engine Components
1. **Rendering Engine**
   - Cross-platform graphics API support Vulkan
   - OpenXR integration layer
   - Scene rendering pipeline
   - Vulkan memory management using VMA

2. **Input System**
   - OpenXR input handling
   - Desktop controller mapping
   - Haptic feedback support
   - GLFW for windowing on desktop

3. **Audio System**
   - 3D spatial audio using OpenAL Soft
   - OpenXR audio integration
   - HRTF (Head-Related Transfer Function) support for realistic VR audio

4. **Physics Engine**
   - Integration with **Jolt Physics** for rigid-body simulation (runtime)
   - **glTF physics authoring via Khronos extensions** (asset source of truth for collision shapes, rigid-body params, materials, filters/joints as the extensions stabilize)
   - Primary extensions to track / consume:
     - **`KHR_physics_rigid_bodies`** (also discussed as `KHR_rigid_bodies` during development) — rigid bodies, motion, mass properties, collision filters
     - **`KHR_implicit_shapes`** — analytic collision volumes (box, sphere, capsule, etc.) alongside mesh-based colliders when needed
   - Loader (`tinygltf` + engine extensions) maps glTF physics nodes into engine physics components that feed Jolt
   - VR-specific collision handling (controllers, world, locomotion) built on the same data path

5. **Asset Pipeline**
   - Import/export formats
   - Resource management
   - Streaming support
   - glTF parsing using tinygltf
   - **Physics properties on models** authored in glTF via the Khronos physics extensions above (not ad-hoc engine-only sidecar formats for production assets)

## Development Roadmap

### Phase 1: Foundation (Weeks 1-4)
- Set up development environment
- Implement basic OpenXR integration
- Create core engine architecture
- Basic rendering pipeline with Vulkan and VK-Bootstrap
- Integrate GLFW for desktop windowing
- Setup VMA for Vulkan memory management
- Integrate OpenAL Soft for audio system

### Phase 2: Desktop Development Mode (Weeks 5-8)
- Implement desktop input handling
- Create controller visualization system
- Develop debugging tools
- Integrate with development workflow
- Implement GLM for math operations
- **Physics foundation (when rendering path is stable enough):**
  - Jolt integration (world step, rigid bodies, collision layers)
  - glTF load path for Khronos physics extensions (`KHR_physics_rigid_bodies`, `KHR_implicit_shapes` / successors as ratified)
  - Map authored shapes + body properties → Jolt colliders / bodies

### Phase 3: VR Features (Weeks 9-12)
- Full OpenXR integration
- VR-specific features
- **Reverse-Z depth** (near→1 / far→0, `GREATER` compare, clear 0) coordinated with stereo/multiview
- Same-frame occlusion (depth prepass → Hi-Z → shade) is **landed on desktop** (no hysteresis). Quest work: multiview / per-eye prepass + reverse-Z HZB compares.
- Performance optimization
- Testing and debugging

### Phase 4: Meta Quest Support (Weeks 13-16)
- Quest-specific optimizations (TBDR, GMEM, reverse-Z + multiview depth path validated on device)
- App packaging and deployment
- Testing on Quest hardware
- Final integration

## Tools and Technologies
- Programming Language: C++ 20
- Graphics API: Vulkan
- Windowing: GLFW
- Math Library: GLM
- Physics Engine: Jolt Physics (runtime simulation)
- Physics assets: Khronos glTF physics extensions (`KHR_physics_rigid_bodies`, `KHR_implicit_shapes` and related as finalized)
- VR Runtime: OpenXR
- glTF Parsing: tinygltf
- Vulkan Setup: VK-Bootstrap
- Vulkan Memory Management: VMA
- Audio System: OpenAL Soft
- IDE: VS Code or Visual Studio
- Version Control: Git
- Build System: CMake
- Testing Framework: Google Test

## Risks and Mitigations
- OpenXR compatibility issues: Regular testing on multiple hardware
- Performance on Quest devices: Early optimization and profiling
- Cross-platform compatibility: Continuous testing on all target platforms

## Success Metrics
- Working OpenXR integration
- Desktop development mode with controller visualization
- Successful compilation and deployment to Meta Quest
- Performance benchmarks on target hardware

