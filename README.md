# Aero Boar VR Game Engine
A C++ VR game engine using Vulkan, OpenXR, Jolt Physics, FMOD, and glTF.
Supports desktop (non-VR), PCVR, and Meta Quest.

## Setup
1. Clone with: git clone --recurse-submodules <repo_url>
2. Install Vulkan SDK: https://vulkan.lunarg.com/
3. For Quest: Install Android NDK and set ANDROID_NDK_HOME
4. Download FMOD Core API (https://fmod.com/download) and place in external/fmod/
5. Run: mkdir build && cd build && cmake .. -G "MinGW Makefiles" && mingw32-make

## Structure
- assets/: glTF models and scenes, audio
- external/: Dependencies (submodules or vendored)
- include/, src/: Engine code
- shaders/: GLSL for Vulkan
- config/: Accessibility settings
- tests/: Unit tests
