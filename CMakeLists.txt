cmake_minimum_required(VERSION 3.20)
project(AeroBoarEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
option(BUILD_VR "Enable VR mode (OpenXR)" ON)
option(BUILD_ANDROID "Build for Android (Quest)" OFF)

# Find Vulkan and glslangValidator
find_package(Vulkan REQUIRED)
find_program(GLSLANG_VALIDATOR glslangValidator HINTS ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found. Install Vulkan SDK.")
endif()

# Define source files
set(SOURCES
    src/main.cpp
    src/core/renderer.cpp
    src/core/skybox.cpp
    src/core/accessibility.cpp
    src/input/input_manager.cpp
    src/input/hand_tracker.cpp
    src/physics/physics_world.cpp
    src/vr/vr_system.cpp
    src/assets/gltf_loader.cpp
)

# Platform-specific sources
if(BUILD_ANDROID)
    set(PLATFORM_SOURCES src/platforms/android/android_main.cpp)
else()
    set(PLATFORM_SOURCES src/platforms/windows/main.cpp src/platforms/windows/window.cpp)
endif()

# Combine sources
list(APPEND SOURCES ${PLATFORM_SOURCES})

# Include directories
set(INCLUDE_DIRS
    include
    external/glfw/include
    external/glm
    external/jolt
    external/openxr/include
    external/tinygltf
    external/fmod/inc
    external/vk-bootstrap/src
    external/vulkan-memory-allocator/include
    ${Vulkan_INCLUDE_DIRS}
)

# Check for vendored FMOD
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/fmod/inc/fmod.hpp")
    message(FATAL_ERROR "FMOD headers not found. Download from https://fmod.com/download and place in external/fmod/inc/")
endif()

# Add submodules
add_subdirectory(external/glfw)
add_subdirectory(external/jolt/Build)
add_subdirectory(external/openxr)
add_subdirectory(external/vk-bootstrap)

# Define executable
if(BUILD_ANDROID)
    add_library(aero_boar_engine SHARED ${SOURCES})
else()
    add_executable(aero_boar_engine ${SOURCES})
endif()

# Include directories
target_include_directories(aero_boar_engine PRIVATE ${INCLUDE_DIRS})

# Link libraries
target_link_libraries(aero_boar_engine PRIVATE
    Vulkan::Vulkan
    glfw
    Jolt
    openxr_loader
)

# FMOD: Link prebuilt libs
if(BUILD_ANDROID)
    find_library(FMOD_LIB NAMES fmod PATHS "${CMAKE_SOURCE_DIR}/external/fmod/lib/android/arm64-v8a")
    if(NOT FMOD_LIB)
        message(FATAL_ERROR "FMOD lib not found in external/fmod/lib/android/arm64-v8a/")
    endif()
    target_link_libraries(aero_boar_engine PRIVATE ${FMOD_LIB})
else()
    find_library(FMOD_LIB NAMES fmod fmodL fmod_vc fmodL_vc PATHS "${CMAKE_SOURCE_DIR}/external/fmod/lib/win64")
    if(NOT FMOD_LIB)
        message(FATAL_ERROR "FMOD lib not found in external/fmod/lib/win64/")
    endif()
    target_link_libraries(aero_boar_engine PRIVATE ${FMOD_LIB})
endif()

# Compile definitions
if(BUILD_VR)
    target_compile_definitions(aero_boar_engine PRIVATE USE_VR=1)
endif()
if(BUILD_ANDROID)
    target_compile_definitions(aero_boar_engine PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
else()
    target_compile_options(aero_boar_engine PRIVATE -Wall -Wextra)
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_link_libraries(aero_boar_engine PRIVATE -static-libgcc -static-libstdc++)
    endif()
endif()

# Shader compilation to SPIR-V
file(GLOB SHADERS shaders/*.vert shaders/*.frag shaders/*.tesc shaders/*.tese)
foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUT "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV_OUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SPIRV_OUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER_NAME} to SPIR-V"
    )
    list(APPEND SPIRV_SHADERS ${SPIRV_OUT})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SPIRV_SHADERS})
add_dependencies(aero_boar_engine shaders)

# Copy assets and config to build dir
file(COPY assets DESTINATION ${CMAKE_BINARY_DIR})
file(COPY config DESTINATION ${CMAKE_BINARY_DIR})

# Android-specific
if(BUILD_ANDROID)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")
    set(ANDROID_NDK $ENV{ANDROID_NDK_HOME})
    if(NOT ANDROID_NDK)
        message(FATAL_ERROR "Set ANDROID_NDK_HOME environment variable")
    endif()
endif()