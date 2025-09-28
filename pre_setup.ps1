# pre_setup.ps1
# Checks for required tools (Git, CMake, Vulkan SDK, Android NDK, C++ compiler) on Windows.
# Run with: .\pre_setup.ps1
# Prerequisites: Run before setup_project.ps1. Excludes IDE checks.

# Stop on errors
$ErrorActionPreference = "Stop"

Write-Host "Checking prerequisites for Aero Boar Engine..."

# Check Git
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error "Git not found. Install from https://git-scm.com/download/win"
    exit 1
}
Write-Host "Git found: $(git --version)"

# Check CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "CMake not found. Install from https://cmake.org/download/"
    exit 1
}
Write-Host "CMake found: $(cmake --version | Select-Object -First 1)"

# Check Vulkan SDK
if (-not $env:VULKAN_SDK -or -not (Test-Path "$env:VULKAN_SDK/Bin/glslangValidator.exe")) {
    Write-Error "Vulkan SDK not found. Install from https://vulkan.lunarg.com/"
    exit 1
}
Write-Host "Vulkan SDK found: $env:VULKAN_SDK"

# Check C++ compiler (MSVC or gcc)
if (-not (Get-Command cl -ErrorAction SilentlyContinue) -and -not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Error "C++ compiler not found. Install MSVC (via Visual Studio) or MinGW (gcc/g++)."
    exit 1
}
if (Get-Command cl -ErrorAction SilentlyContinue) {
    Write-Host "MSVC compiler found"
} elseif (Get-Command g++ -ErrorAction SilentlyContinue) {
    Write-Host "GCC compiler found: $(g++ --version | Select-Object -First 1)"
}

# Check Android NDK (for Quest builds)
$NDK_PATH = $env:ANDROID_NDK_HOME
if (-not $NDK_PATH) {
    # Try common Visual Studio NDK path
    $NDK_PATH = "$env:ProgramData\Microsoft\AndroidNDK64\android-ndk-r27c"
}
if (-not $NDK_PATH -or -not (Test-Path "$NDK_PATH/source.properties")) {
    Write-Warning "Android NDK not found. Required for Quest builds. Install via Visual Studio or download from https://developer.android.com/ndk/downloads"
    Write-Warning "Set ANDROID_NDK_HOME to NDK path (e.g., C:\ProgramData\Microsoft\AndroidNDK64\android-ndk-r27c)"
} else {
    Write-Host "Android NDK found: $NDK_PATH"
    # Set ANDROID_NDK_HOME if not already set
    if (-not $env:ANDROID_NDK_HOME) {
        [System.Environment]::SetEnvironmentVariable("ANDROID_NDK_HOME", $NDK_PATH, [System.EnvironmentVariableTarget]::Process)
        Write-Host "Set ANDROID_NDK_HOME to $NDK_PATH for this session"
    }
}

Write-Host "All prerequisites met! Ready to run setup_project.ps1"