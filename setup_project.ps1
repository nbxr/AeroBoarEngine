# setup_project.ps1
# Creates folder structure for Aero Boar VR game engine and initializes dependencies on Windows.
# Run with: .\setup_project.ps1
# Prerequisites: Git installed. FMOD needs manual download (placeholders created).

# Stop on errors
$ErrorActionPreference = "Stop"

$PROJECT_DIR = "aero_boar_engine"
Write-Host "Setting up project in $PROJECT_DIR..."

# Create root directory
if (-not (Test-Path $PROJECT_DIR)) {
    New-Item -ItemType Directory -Path $PROJECT_DIR | Out-Null
}
Set-Location $PROJECT_DIR

# Initialize Git repo if not already
if (-not (Test-Path ".git")) {
    git init
    Write-Host "Initialized Git repository"
}

# Create folder structure
$dirs = @(
    "assets/models",
    "assets/scenes",
    "assets/audio/sfx",
    "assets/audio/voice",
    "external/fmod/inc",
    "external/fmod/lib/win64",
    "external/fmod/lib/android/arm64-v8a",
    "external/glfw",
    "external/glm",
    "external/jolt",
    "external/openxr",
    "external/tinygltf",
    "external/vk-bootstrap",
    "external/vulkan-memory-allocator",
    "include/core",
    "include/input",
    "include/physics",
    "include/vr",
    "include/assets",
    "src/core",
    "src/input",
    "src/physics",
    "src/vr",
    "src/assets",
    "src/platforms/windows",
    "src/platforms/android",
    "shaders",
    "config",
    "tests"
)
foreach ($dir in $dirs) {
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
}

# Create placeholder asset files
$assets = @(
    "assets/models/hand.glb",
    "assets/models/cube.glb",
    "assets/scenes/test_scene.glb",
    "assets/audio/sfx/placeholder.txt",
    "assets/audio/voice/placeholder.txt"
)
foreach ($asset in $assets) {
    New-Item -ItemType File -Path $asset -Force | Out-Null
}

# Create placeholder for FMOD (vendored)
$FMOD_README = @"
FMOD Core API: Download from https://fmod.com/download
- Place headers in external/fmod/inc/
- Place prebuilt libs in external/fmod/lib/win64/ (Windows) and external/fmod/lib/android/arm64-v8a/ (Quest)
- Ensure license.txt is included
"@
Set-Content -Path "external/fmod/README.txt" -Value $FMOD_README
New-Item -ItemType File -Path "external/fmod/license.txt" -Force | Out-Null

# Initialize submodules
$submodules = @{
    "external/glfw" = "https://github.com/glfw/glfw.git"
    "external/glm" = "https://github.com/g-truc/glm.git"
    "external/jolt" = "https://github.com/jrouwe/JoltPhysics.git"
    "external/openxr" = "https://github.com/KhronosGroup/OpenXR-SDK.git"
    "external/tinygltf" = "https://github.com/syoyo/tinygltf.git"
    "external/vk-bootstrap" = "https://github.com/charles-lunarg/vk-bootstrap.git"
    "external/vulkan-memory-allocator" = "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git"
}
foreach ($submodule in $submodules.GetEnumerator()) {
    if (-not (Test-Path $submodule.Key)) {
        git submodule add $submodule.Value $submodule.Key
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Failed to add submodule $($submodule.Key)"
            exit 1
        }
    }
}

# Initialize and update submodules
git submodule update --init --recursive
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to initialize submodules. Check network or Git permissions."
    exit 1
}
Write-Host "Submodules initialized"

# Create basic README
$README = @"
# Aero Boar VR Game Engine
A C++ VR game engine using Vulkan, OpenXR, Jolt Physics, FMOD, and glTF.
Supports desktop (non-VR), PCVR, and Meta Quest.

## Setup
1. Clone with: git clone --recurse-submodules <repo_url>
2. Install Vulkan SDK: https://vulkan.lunarg.com/
3. For Quest: Install Android NDK and set ANDROID_NDK_HOME
4. Download FMOD Core API (https://fmod.com/download) and place in external/fmod/
5. Run: mkdir build && cd build && cmake .. -G `"MinGW Makefiles`" && mingw32-make

## Structure
- assets/: glTF models and scenes, audio
- external/: Dependencies (submodules or vendored)
- include/, src/: Engine code
- shaders/: GLSL for Vulkan
- config/: Accessibility settings
- tests/: Unit tests
"@
Set-Content -Path "README.md" -Value $README

# Create placeholder accessibility config
$CONFIG = @"
{
  "highContrast": false,
  "handMode": "right",
  "snapTurn": false,
  "seatedMode": false
}
"@
Set-Content -Path "config/accessibility.json" -Value $CONFIG

# Create placeholder source files
$sourceFiles = @(
    "src/main.cpp",
    "src/core/renderer.cpp",
    "src/core/skybox.cpp",
    "src/core/accessibility.cpp",
    "src/input/input_manager.cpp",
    "src/input/hand_tracker.cpp",
    "src/physics/physics_world.cpp",
    "src/vr/vr_system.cpp",
    "src/assets/gltf_loader.cpp",
    "src/platforms/windows/main.cpp",
    "src/platforms/windows/window.cpp",
    "src/platforms/android/android_main.cpp",
    "include/core/renderer.hpp",
    "include/core/skybox.hpp",
    "include/core/accessibility.hpp",
    "include/input/input_manager.hpp",
    "include/input/hand_tracker.hpp",
    "include/physics/physics_world.hpp",
    "include/vr/vr_system.hpp",
    "include/assets/gltf_loader.hpp",
    "shaders/pbr.vert",
    "shaders/pbr.frag",
    "shaders/cel_shading.frag",
    "shaders/skybox.frag",
    "shaders/impostor.frag",
    "shaders/tessellation.tesc",
    "shaders/tessellation.tese",
    "tests/test_renderer.cpp",
    "tests/test_physics.cpp"
)
foreach ($file in $sourceFiles) {
    New-Item -ItemType File -Path $file -Force | Out-Null
}

# Create .gitignore
$GITIGNORE = @"
build/
*.o
*.so
*.lib
*.exe
*.apk
"@
Set-Content -Path ".gitignore" -Value $GITIGNORE

# Commit initial structure
git add .
git commit -m "Initial project structure and dependencies"

Write-Host "Setup complete! Folder structure created in $PROJECT_DIR"
Write-Host "Next steps:"
Write-Host "- Download FMOD, place in external/fmod/ as per README.txt"
Write-Host "- Run: mkdir build; cd build; cmake .. -G `"MinGW Makefiles`"; mingw32-make"
Write-Host "- For Quest: cmake .. -DBUILD_ANDROID=ON -DANDROID_NDK_HOME=$env:ANDROID_NDK_HOME"
Write-Host "- Push to remote: git remote add origin <url>; git push -u origin main"