# Aero Boar VR Game Engine
A C++ VR game engine using Vulkan, OpenXR, Jolt Physics, FMOD, and glTF.
Supports desktop (non-VR), PCVR, and Meta Quest.

## Current Status
✅ **Phase 1 Complete**: Vulkan triangle rendering with robust window handling, swapchain recreation, proper synchronization, and advanced frame management system.

✅ **Phase 2 Complete**: glTF asset loading with background threads, transfer queues, and 3D camera controls (mouse look + WASD movement).

✅ **Phase 2.5 Complete**: Input management system with window abstraction layer, action-based input mapping, and VR-ready architecture.

## Current Features
- **3D Rendering**: Vulkan-based rendering with PBR shaders and MVP matrices
- **Asset Loading**: Asynchronous glTF model loading with background threads
- **Camera Controls**: Mouse look and WASD movement with proper 3D navigation
- **Input System**: Action-based input mapping with VR-ready abstraction
- **Window Management**: Cross-platform window abstraction (desktop/VR ready)
- **Memory Management**: VMA integration for efficient GPU memory allocation
- **Frame Management**: Advanced per-frame resource tracking and synchronization
- **Transfer Queues**: Asynchronous GPU memory operations without blocking render thread

## Clone and Setup
1. Clone with submodules:
   ```bash
   git clone --recurse-submodules https://github.com/nbxr/AeroBoarEngine.git
   ```
2. Install required tools:
   - **Vulkan SDK**: https://vulkan.lunarg.com/
   - **Visual Studio 2022**: For MSVC compiler (recommended)
   - **CMake**: Build system
   - **Git**: For submodules
3. For Quest builds: Install Android NDK (R27C) and set `ANDROID_NDK_HOME`
4. Download FMOD Core API (https://fmod.com/download) and place in `external/fmod/`

## Build Instructions
### Desktop (Windows)
```powershell
cd aero_boar_engine
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### Quest (Android)
```powershell
cmake .. -G "Visual Studio 17 2022" -DBUILD_ANDROID=ON -DANDROID_NDK_HOME=$env:ANDROID_NDK_HOME
cmake --build . --config Release
```

## Project Structure
- `assets/`: glTF models and scenes, audio files
- `external/`: Dependencies (submodules: GLFW, GLM, Jolt, OpenXR, tinygltf, VK-Bootstrap, VMA; vendored: FMOD)
- `include/`, `src/`: Engine source code (core, input, physics, vr, assets, platforms)
- `shaders/`: GLSL shaders for Vulkan (PBR, cel shading, skybox, impostor, tessellation)
- `config/`: Accessibility settings
- `tests/`: Unit tests
- `docs/`: Project documentation and development plan

## Development Phases
- ✅ **Phase 1**: Vulkan initialization and triangle rendering
- ✅ **Phase 2**: glTF asset loading with background threads and transfer queues
- ✅ **Phase 2.5**: Input management system with window abstraction
- 🔄 **Phase 3**: Game object management system (next)
- ⏳ **Phase 4**: Physics integration with Jolt
- ⏳ **Phase 5**: VR support with OpenXR
- ⏳ **Phase 6**: Audio and accessibility features
- ⏳ **Phase 7**: Quest build and testing

See `docs/project-plan.md` for detailed development roadmap.
