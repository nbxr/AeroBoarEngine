cmake_minimum_required(VERSION 3.20)
project(AeroBoarEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure for static linking
set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a)
set(BUILD_SHARED_LIBS OFF)

# Static runtime library for MSVC
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
    
endif()

# Additional static linking configuration
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# Set Visual Studio solution and project organization
if(MSVC)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")
endif()

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
    src/core/transfer_manager.cpp
    src/core/window_factory.cpp
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
    set(PLATFORM_SOURCES src/platforms/desktop/desktop_window.cpp)
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

# Configure external libraries for static linking BEFORE adding subdirectories
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# Check for vendored FMOD
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/fmod/inc/fmod.hpp")
    message(FATAL_ERROR "FMOD headers not found. Download from https://fmod.com/download and place in external/fmod/inc/")
endif()

# Configure external libraries for static runtime
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Add submodules
add_subdirectory(external/glfw)
add_subdirectory(external/jolt/Build)
add_subdirectory(external/tinygltf)

# Configure OpenXR build options to avoid sanitizers dependency
set(BUILD_ALL_EXTENSIONS OFF CACHE BOOL "Build loader and layers with all extensions")
set(BUILD_LOADER_WITH_EXCEPTION_HANDLING ON CACHE BOOL "Enable exception handling in the loader")
set(BUILD_TESTS OFF CACHE BOOL "Build tests")
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "Build conformance tests")
set(BUILD_API_LAYERS OFF CACHE BOOL "Build API layers")
set(DYNAMIC_LOADER OFF CACHE BOOL "Build OpenXR loader as static library")

add_subdirectory(external/openxr)
add_subdirectory(external/vk-bootstrap)
add_subdirectory(external/vulkan-memory-allocator)

# Configure external library output directories for Visual Studio-like structure
if(MSVC)
    # GLFW
    set_target_properties(glfw PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/External/GLFW/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/External/GLFW/Release"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/External/GLFW/Debug"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/External/GLFW/Release"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/External/GLFW/Debug"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/External/GLFW/Release"
    )
    
    # OpenXR
    set_target_properties(openxr_loader PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/External/OpenXR/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/External/OpenXR/Release"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/External/OpenXR/Debug"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/External/OpenXR/Release"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/External/OpenXR/Debug"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/External/OpenXR/Release"
    )
    
    # VK-Bootstrap
    set_target_properties(vk-bootstrap PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/External/VK-Bootstrap/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/External/VK-Bootstrap/Release"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/External/VK-Bootstrap/Debug"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/External/VK-Bootstrap/Release"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/External/VK-Bootstrap/Debug"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/External/VK-Bootstrap/Release"
    )
endif()

# Define executable
if(BUILD_ANDROID)
    add_executable(aero_boar_engine ${SOURCES})
else()
    add_executable(aero_boar_engine ${SOURCES})
endif()

# Set Visual Studio-like output directories
if(MSVC)
    set_target_properties(aero_boar_engine PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/AeroBoarEngine/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/AeroBoarEngine/Debug"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/AeroBoarEngine/Debug"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        
        PDB_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/AeroBoarEngine/Debug"
        PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
        PDB_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/AeroBoarEngine/Release"
    )
endif()

# Include directories
target_include_directories(aero_boar_engine PRIVATE ${INCLUDE_DIRS})

# Link libraries
target_link_libraries(aero_boar_engine PRIVATE
    Vulkan::Vulkan
    glfw
    Jolt
    openxr_loader
    vk-bootstrap
    VulkanMemoryAllocator
    tinygltf
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
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(aero_boar_engine PRIVATE -Wall -Wextra)
                if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
                    target_link_libraries(aero_boar_engine PRIVATE -static-libgcc -static-libstdc++)
                endif()
            elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
                target_compile_options(aero_boar_engine PRIVATE /W4)
                # Force static runtime library for consistency with Jolt
                target_compile_options(aero_boar_engine PRIVATE $<$<CONFIG:Debug>:/MTd> $<$<CONFIG:Release>:/MT>)
            endif()
        endif()

# Shader compilation to SPIR-V
file(GLOB SHADERS shaders/*.vert shaders/*.frag shaders/*.tesc shaders/*.tese)
foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUT "${CMAKE_BINARY_DIR}/Shaders/Debug/${SHADER_NAME}.spv")
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

# Set shader target properties for Visual Studio-like structure
if(MSVC)
    set_target_properties(shaders PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Shaders/Debug"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Shaders/Release"
        FOLDER "Shaders"
    )
    
    # Set folder organization for all targets
    set_target_properties(aero_boar_engine PROPERTIES FOLDER "AeroBoarEngine")
    set_target_properties(glfw PROPERTIES FOLDER "External/GLFW")
    set_target_properties(openxr_loader PROPERTIES FOLDER "External/OpenXR")
    set_target_properties(vk-bootstrap PROPERTIES FOLDER "External/VK-Bootstrap")
endif()

# Copy assets and config to build dir
file(COPY assets DESTINATION ${CMAKE_BINARY_DIR}/Assets)
file(COPY config DESTINATION ${CMAKE_BINARY_DIR}/Config)

# Copy shaders, assets, and config to output directory for each configuration
if(NOT BUILD_ANDROID)
    # Copy shaders to Debug output directory
    add_custom_command(TARGET aero_boar_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:aero_boar_engine>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/Shaders/Debug $<TARGET_FILE_DIR:aero_boar_engine>/shaders
        COMMENT "Copying shaders to output directory"
    )
    
    # Copy assets to Debug output directory
    add_custom_command(TARGET aero_boar_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:aero_boar_engine>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/Assets/assets $<TARGET_FILE_DIR:aero_boar_engine>/assets
        COMMENT "Copying assets to output directory"
    )
    
    # Copy config to Debug output directory
    add_custom_command(TARGET aero_boar_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:aero_boar_engine>/config
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/Config/config $<TARGET_FILE_DIR:aero_boar_engine>/config
        COMMENT "Copying config to output directory"
    )
endif()

# Android-specific
if(BUILD_ANDROID)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")
    set(ANDROID_NDK $ENV{ANDROID_NDK_HOME})
    if(NOT ANDROID_NDK)
        message(FATAL_ERROR "Set ANDROID_NDK_HOME environment variable")
    endif()
endif()