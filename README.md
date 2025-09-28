# Aero Boar VR Game Engine
A C++ VR game engine using Vulkan, OpenXR, Jolt Physics, FMOD, and glTF.
Supports desktop (non-VR), PCVR, and Meta Quest.

## Current Status
✅ **Phase 1 Complete**: Vulkan triangle rendering with robust window handling, swapchain recreation, proper synchronization, and advanced frame management system.

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
- 🔄 **Phase 2**: glTF asset loading (next)
- ⏳ **Phase 3**: Physics integration with Jolt
- ⏳ **Phase 4**: VR support with OpenXR
- ⏳ **Phase 5**: Audio and accessibility features
- ⏳ **Phase 6**: Quest build and testing

See `docs/project-plan.md` for detailed development roadmap.
