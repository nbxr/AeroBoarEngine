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

### Phase 2: glTF Asset Loading (1-2 weeks)
- **Goal**: Load and render `assets/models/cube.glb`.
- **Tasks**:
  - Implement `src/assets/gltf_loader.cpp` and `include/assets/gltf_loader.hpp` using tinygltf.
  - Parse meshes, materials, textures from glTF.
  - Integrate with renderer to display cube.
  - Test: Load and render `cube.glb` on desktop.
- **Files**: `src/assets/gltf_loader.*`, `assets/models/cube.glb`.

### Phase 3: Physics Integration (2-3 weeks)
- **Goal**: Add physics to cube using Jolt Physics.
- **Tasks**:
  - Implement `src/physics/physics_world.cpp` and `include/physics/physics_world.hpp`.
  - Create physics world, add cube as dynamic body.
  - Simulate gravity and collisions.
  - Test: Cube falls and collides with a ground plane.
- **Files**: `src/physics/physics_world.*`, `tests/test_physics.cpp`.

### Phase 4: VR Support (2-3 weeks)
- **Goal**: Enable VR rendering with OpenXR.
- **Tasks**:
  - Implement `src/vr/vr_system.cpp` and `include/vr/vr_system.hpp`.
  - Initialize OpenXR session, handle VR input (hand tracking).
  - Update renderer for stereo rendering.
  - Test: Render cube in VR on PCVR or Quest.
- **Files**: `src/vr/vr_system.*`, `src/input/hand_tracker.*`.

### Phase 5: Audio and Accessibility (1-2 weeks)
- **Goal**: Add audio and accessibility features.
- **Tasks**:
  - Implement FMOD audio in `src/core/audio.cpp` (create new).
  - Load `config/accessibility.json` for settings (high contrast, snap turn).
  - Test: Play sound from `assets/audio/sfx/` and toggle accessibility.
- **Files**: `src/core/audio.cpp`, `include/core/audio.hpp`, `config/accessibility.json`.

### Phase 6: Quest Build and Testing (2 weeks)
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
- Start Phase 1: Implement Vulkan init in `renderer.hpp/cpp`.
- Commit regularly and push to remote.
- Use `tests/` for unit tests (e.g., `test_renderer.cpp`).