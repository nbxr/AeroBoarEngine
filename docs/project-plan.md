# Aero Boar VR Game Engine Project Plan

## Overview
The **Aero Boar Engine** is a C++ VR game engine using Vulkan for rendering, OpenXR for VR, Jolt Physics for physics, FMOD for audio, and glTF for asset loading. It targets desktop (non-VR and PCVR) and Meta Quest (Android/ARM64). This plan outlines development phases, structure, and dependencies for implementation in Cursor (with Grok as the agent).

## Project Goals
- Build a lightweight VR game engine supporting:
  - Desktop: Non-VR and PCVR (e.g., Oculus Rift, Valve Index).
  - Meta Quest: Standalone VR via Android/ARM64.
- Features: PBR rendering, physics, hand tracking, accessibility (high contrast, snap turn, seated mode).
- Use glTF for models and scenes, avoiding USD for simplicity.
- Ensure portability with Git submodules and minimal vendored dependencies.

## Repository Structure
Located at `W:/git/aero_boar_engine/`:
- `assets/`: glTF models (`hand.glb`, `cube.glb`), scenes (`test_scene.glb`), audio.
- `external/`: Dependencies:
  - Submodules: GLFW, GLM, Jolt Physics, OpenXR, tinygltf, VK-Bootstrap, VMA.
  - Vendored: FMOD (`external/fmod/inc/`, `lib/win64/`, `lib/android/arm64-v8a/`).
- `include/`, `src/`: Engine code (core, input, physics, vr, assets, platforms).
- `shaders/`: GLSL for Vulkan (PBR, cel shading, skybox, impostor, tessellation).
- `config/`: Accessibility settings (`accessibility.json`).
- `tests/`: Unit tests.
- `build/`: CMake build output (ignored in `.gitignore`).

## Dependencies
- **Tools** (verified by `pre_setup.ps1`):
  - Git: For repo and submodules.
  - CMake: Build system.
  - Vulkan SDK: Headers, libs, glslangValidator.
  - gcc (MinGW): Compiler for Windows.
  - Android NDK (R27C): Quest builds, set via `ANDROID_NDK_HOME`.
- **Libraries**:
  - GLFW: Windowing (desktop).
  - GLM: Math.
  - Jolt Physics: Physics simulation.
  - OpenXR: VR runtime.
  - tinygltf: glTF parsing.
  - VK-Bootstrap: Vulkan setup.
  - VMA: Vulkan memory management.
  - FMOD: Audio (vendored).

## Architectural Principles

### Multi-Threading and Asynchronous Operations
The engine is designed with modern multi-threaded architecture in mind:

- **Background Threading**: Asset loading, physics simulation, and audio processing run on dedicated background threads
- **Transfer Queue Operations**: GPU memory operations use dedicated transfer queues to avoid blocking graphics operations
- **Frame Management**: The Frame management system ensures proper synchronization between threads without blocking
- **Non-Blocking Design**: Main render thread must never be blocked by background operations

### Synchronization Strategy
- **Per-Frame Resource Tracking**: Each frame tracks its own resources (semaphores, fences, command buffers)
- **Smart Fence Waiting**: Uses `vkGetFenceStatus` for targeted synchronization instead of broad `vkDeviceWaitIdle`
- **Queue-Specific Operations**: Different operations use appropriate queues (graphics, transfer, compute)
- **Thread-Safe Resource Management**: All resource creation/destruction is properly synchronized

## Development Phases
### Phase 1: Vulkan Initialization (1-2 weeks)
- **Goal**: Render a triangle on desktop using Vulkan.
- **Tasks**:
  - Implement `include/core/renderer.hpp` and `src/core/renderer.cpp`:
    - Initialize Vulkan instance, device, queues with VK-Bootstrap.
    - Use VMA for memory allocation.
    - Create GLFW window (`src/platforms/windows/window.cpp`).
    - Set up swapchain, render pass, pipeline.
    - Render a hard-coded triangle using `shaders/pbr.vert` and `pbr.frag`.
  - Update `src/main.cpp` for desktop entry point.
  - Test: Run on Windows, verify triangle renders.
- **Files**: `src/main.cpp`, `src/core/renderer.cpp`, `include/core/renderer.hpp`, `src/platforms/windows/*`, `shaders/pbr.*`.
- **Additional Accomplishments**:
  - ✅ **Dynamic Viewport and Scissor**: Implemented proper viewport and scissor handling for window resizing
  - ✅ **Robust Window Resize Handling**: Complete swapchain recreation with proper synchronization
  - ✅ **Frame Skipping Mechanism**: Implemented proper frame skipping during swapchain recreation to prevent deadlocks
  - ✅ **Vulkan Synchronization**: Fixed all fence and semaphore synchronization issues (no validation errors)
  - ✅ **Visual Studio 2022 Build**: Switched from MinGW to MSVC for better Vulkan library compatibility
  - ✅ **Static Linking Support**: Configured build system for static runtime linking
  - ✅ **Shader Management**: Automatic SPIR-V compilation and deployment to output directory
  - ✅ **Per-Image Semaphores**: Implemented proper per-image semaphore handling for swapchain synchronization
  - ✅ **Fence Timeout Protection**: Added timeout to fence waits to prevent infinite blocking
  - ✅ **Clean Production Code**: Removed all debug output for production-ready application
  - ✅ **Cross-Platform Ready**: Foundation prepared for both desktop and VR rendering paths
  - ✅ **Frame Management System**: Implemented comprehensive per-frame resource tracking with Frame and ImageResources structs
  - ✅ **Advanced Synchronization**: Smart fence waiting with vkGetFenceStatus for optimal performance
  - ✅ **Instantaneous Resize**: Window resizing now works without lag or freezing
  - ✅ **Zero Validation Errors**: All Vulkan validation errors resolved with proper resource lifecycle management

### Phase 1.5: VMA Integration ✅ **COMPLETE**
- **Goal**: Integrate Vulkan Memory Allocator for efficient memory management.
- **Tasks**:
  - ✅ Add VMA to build system and include headers.
  - ✅ Initialize VMA allocator in renderer initialization.
  - ✅ Refactor vertex buffer creation to use VMA.
  - ✅ Replace manual memory allocation with VMA calls.
  - ✅ Test: Ensure triangle still renders correctly with VMA.
- **Files**: `src/core/renderer.cpp`, `include/core/renderer.hpp`, `CMakeLists.txt`.
- **Benefits**: 
  - ✅ Memory pooling and efficient allocation
  - ✅ Automatic memory type selection
  - ✅ Built-in memory statistics and debugging
  - ✅ Foundation for asset loading memory management
  - ✅ Static linking for standalone deployment
  - ✅ Runtime library consistency: All libraries use static runtime (MT/MTd)
  - ✅ Jolt Physics integration: Physics library successfully integrated

### Phase 2: glTF Asset Loading (1-2 weeks) ✅ **COMPLETE**
- **Goal**: Load and render `assets/models/cube.glb` using background threads and transfer queues.
- **Architecture**: 
  - **Background Asset Loading**: Use dedicated background threads for glTF parsing and texture processing
  - **Transfer Queue Integration**: Utilize Vulkan transfer queues for asynchronous GPU memory operations
  - **Frame Management Integration**: Leverage the existing Frame management system for proper synchronization
  - **Non-Blocking Operations**: Asset loading operations must not block the main render thread
- **Tasks**:
  - ✅ Implement `src/assets/gltf_loader.cpp` and `include/assets/gltf_loader.hpp` using tinygltf.
  - ✅ Create background thread pool for asset loading operations.
  - ✅ Set up Vulkan transfer queue for GPU memory transfers.
  - ✅ Parse meshes, materials, textures from glTF on background threads.
  - ✅ Implement asynchronous texture upload and buffer staging.
  - ✅ Integrate with renderer's frame management system for proper synchronization.
  - ✅ Test: Load and render `cube.glb` on desktop without blocking main thread.
  - ✅ **Camera Controls**: Implemented mouse look and WASD movement with proper 3D rendering
  - ✅ **Input Handling**: Added GLFW input callbacks for mouse and keyboard
  - ✅ **3D Rendering Pipeline**: Updated shaders and uniform buffers for proper MVP matrices
- **Files**: `src/assets/gltf_loader.*`, `assets/models/cube.glb`, `src/core/transfer_manager.*`, `shaders/pbr.*`.

### Phase 2.5: Input Management System (1 week) ✅ **COMPLETE**
- **Goal**: Implement abstracted input management system for desktop controls with VR-ready architecture.
- **Architecture**:
  - **Input Abstraction Layer**: Abstract input types to support both desktop and VR input seamlessly
  - **Action-Based Input**: Map physical inputs (mouse, keyboard, VR controllers) to logical actions
  - **Pose and Movement Separation**: Separate head movement from body movement for VR compatibility
  - **Future VR Integration**: Design system to easily attach OpenXR input types without code changes
- **Tasks**:
  - ✅ Implement `src/input/input_manager.cpp` and `include/input/input_manager.hpp`.
  - ✅ Create input action mapping system (e.g., "look", "move_forward", "move_strafe").
  - ✅ Abstract mouse input as "head movement" for VR compatibility.
  - ✅ Abstract keyboard input as "player pose movement" for VR compatibility.
  - ✅ Implement input state management with proper event handling.
  - ✅ Create input binding system for customizable controls.
  - ✅ Integrate with existing camera system in renderer.
  - ✅ Design VR input attachment points for future OpenXR integration.
  - ✅ Test: Verify input abstraction works with current desktop controls.
  - ✅ **Window Abstraction**: Abstracted GLFW dependencies from core renderer for cross-platform compatibility
  - ✅ **Desktop Window Implementation**: Created GLFW-based desktop window implementation with proper callback handling
  - ✅ **Input Callback Integration**: Seamless integration of mouse and keyboard callbacks through abstract window interface
  - ✅ **Mouse Look Controls**: Implemented smooth mouse look with proper delta handling and sensitivity
  - ✅ **WASD Movement**: Added keyboard movement controls with proper camera-relative movement
  - ✅ **Escape Key Exit**: Added application exit functionality via Escape key
  - ✅ **Reset Camera**: Implemented 'R' key for camera reset to initial position
- **Files**: `src/input/input_manager.*`, `include/input/input_manager.hpp`, `src/core/window_interface.*`, `src/platforms/desktop/desktop_window.*`, `src/core/window_factory.*`, updates to `src/core/renderer.cpp`.
- **Key Features**:
  - ✅ Action-based input mapping system
  - ✅ Head movement abstraction (mouse → VR head tracking)
  - ✅ Player movement abstraction (keyboard → VR controller movement)
  - ✅ Input state management and event handling
  - ✅ Customizable input bindings
  - ✅ VR-ready architecture for future OpenXR integration
  - ✅ Cross-platform window abstraction layer
  - ✅ Desktop-specific window implementation with GLFW
  - ✅ Factory pattern for window creation
  - ✅ Complete separation of platform-specific code from core engine

### Phase 3: Game Object Management System (1-2 weeks)
- **Goal**: Implement efficient game object management with GPU buffer optimization and memory tracking.
- **Architecture**:
  - **Game Object Buffers**: Transform matrices stored in efficient GPU buffers for rendering
  - **Handle-Based System**: Game objects use handles for fast lookup and retrieval
  - **Per-Frame Data Support**: Support for dynamic per-frame game object data
  - **Smart Buffer Management**: Intelligent buffer patching vs. recreation based on change patterns
  - **Memory Efficiency**: Advanced memory tracking and reuse for optimal performance
- **Tasks**:
  - Implement `src/core/game_object_manager.cpp` and `include/core/game_object_manager.hpp`.
  - Create game object buffer management system with VMA integration.
  - Design handle-based game object lookup system for O(1) access.
  - Implement per-frame data management for dynamic game objects.
  - Create smart buffer update strategy (patch vs. recreate) based on change analysis.
  - Integrate with frame management system for proper synchronization.
  - Add memory tracking and reuse mechanisms for efficient updates.
  - Test: Create multiple game objects with transforms and verify efficient GPU buffer updates.
- **Files**: `src/core/game_object_manager.*`, `include/core/game_object_manager.hpp`, `tests/test_game_objects.cpp`.
- **Key Features**:
  - Transform matrix storage in game object buffers
  - Handle-based game object retrieval system
  - Per-frame data support for dynamic objects
  - Efficient GPU buffer update strategies
  - Memory management and reuse tracking
  - Integration with VMA and frame management systems

### Phase 4: Physics Integration (2-3 weeks)
- **Goal**: Add physics to cube using Jolt Physics with background simulation threads.
- **Tasks**:
  - Implement `src/physics/physics_world.cpp` and `include/physics/physics_world.hpp`.
  - Create physics world with dedicated simulation thread.
  - Add cube as dynamic body with proper synchronization.
  - Implement physics-to-render synchronization using frame management system.
  - Integrate physics bodies with game object management system.
  - Simulate gravity and collisions on background thread.
  - Test: Cube falls and collides with a ground plane without blocking render thread.
- **Files**: `src/physics/physics_world.*`, `tests/test_physics.cpp`.

### Phase 5: VR Support (2-3 weeks)
- **Goal**: Enable VR rendering with OpenXR.
- **Tasks**:
  - Implement `src/vr/vr_system.cpp` and `include/vr/vr_system.hpp`.
  - Initialize OpenXR session, handle VR input (hand tracking).
  - Update renderer for stereo rendering.
  - Integrate VR rendering with game object management system.
  - Test: Render cube in VR on PCVR or Quest.
- **Files**: `src/vr/vr_system.*`, `src/input/hand_tracker.*`.

### Phase 6: Audio and Accessibility (1-2 weeks)
- **Goal**: Add audio and accessibility features.
- **Tasks**:
  - Implement FMOD audio in `src/core/audio.cpp` (create new).
  - Load `config/accessibility.json` for settings (high contrast, snap turn).
  - Integrate audio system with game object management for spatial audio.
  - Test: Play sound from `assets/audio/sfx/` and toggle accessibility.
- **Files**: `src/core/audio.cpp`, `include/core/audio.hpp`, `config/accessibility.json`.

### Phase 7: Quest Build and Testing (2 weeks)
- **Goal**: Build and test on Meta Quest.
- **Tasks**:
  - Update `src/platforms/android/android_main.cpp` for Android entry point.
  - Build with `cmake .. -DBUILD_ANDROID=ON -DANDROID_NDK_HOME=$env:ANDROID_NDK_HOME`.
  - Deploy to Quest via ADB.
  - Test: Run triangle and cube rendering on Quest.
- **Files**: `src/platforms/android/android_main.cpp`.

## Build Instructions
- **Setup**: Run `pre_setup.ps1`, then `setup_project.ps1` to create repo and submodules.
- **Place FMOD**: Headers in `external/fmod/inc/`, libs in `external/fmod/lib/win64/` and `lib/android/arm64-v8a/`.
- **Build (Desktop)**:
  ```powershell
  cd W:/git/aero_boar_engine
  mkdir build
  cd build
  cmake .. -G "MinGW Makefiles"
  mingw32-make
  ```
- **Build (Quest)**:
  ```powershell
  cmake .. -DBUILD_ANDROID=ON -DANDROID_NDK_HOME=$env:ANDROID_NDK_HOME
  mingw32-make
  ```
- **Git**: Push to remote (`git push -u origin main`).

## Notes for Cursor
- Use Grok to assist with C++ code completion, Vulkan/OpenXR APIs, and debugging.
- Focus on one phase at a time, starting with Vulkan init.
- Test incrementally: Desktop first, then Quest.
- Repo is ~20MB (with submodules and FMOD).

## Next Steps
- ✅ **Phase 1 Complete**: Vulkan initialization with advanced frame management system
- ✅ **Phase 2 Complete**: glTF asset loading with background threads and transfer queues, plus camera controls
- ✅ **Phase 2.5 Complete**: Input management system with window abstraction and VR-ready architecture
- 🚀 **Ready for Phase 3**: Implement game object management system with GPU buffer optimization
- Commit regularly and push to remote.
- Use `tests/` for unit tests (e.g., `test_renderer.cpp`).