cmake_minimum_required(VERSION 3.5...3.22)

project(Aero_Boar_Engine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -----------------------------------------------------------------------------
# Basic sanity checks
# -----------------------------------------------------------------------------
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "32-bit build detected (i686 / mingw32 style).\n"
        "This engine requires a 64-bit toolchain (x86_64 or ARM64).\n"
        "Current compiler: ${CMAKE_CXX_COMPILER}\n\n"
        "On Windows:\n"
        "  - Preferred: cmake -G \"Visual Studio 17 2022\" -A x64\n"
        "  - For 64-bit MinGW: use x86_64-w64-mingw32-gcc/g++ (not the i686 'mingw32' one)\n"
        "Delete your build/ directory and reconfigure with a 64-bit compiler.")
endif()

# Add position-independent code for Linux builds
if(UNIX AND NOT APPLE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# -----------------------------------------------------------------------------
# Shader compilation with glslc
# -----------------------------------------------------------------------------

# Find glslc (part of Vulkan SDK)
if(WIN32)
    find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin)
elseif(APPLE)
    find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
else()
    find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin /usr/bin)
endif()

if(NOT GLSLC)
    message(WARNING "glslc not found. Shaders will not be compiled automatically.")
    set(GLSLC_AVAILABLE OFF)
else()
    message(STATUS "Found glslc: ${GLSLC}")
    set(GLSLC_AVAILABLE ON)
endif()

# Set shader directories
set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)

# Create shader output directory
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Define shader files to compile (add more as needed)
set(SHADER_FILES
    pbr.vert
    pbr.frag
    cull_frustum.comp
    build_indirect.comp
    hzb_copy.comp
    hzb_reduce.comp
)

# Create shader compilation rules
if(GLSLC_AVAILABLE)
    foreach(SHADER_FILE ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
        get_filename_component(SHADER_EXT ${SHADER_FILE} EXT)
        set(SHADER_SPIRV_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
        set(SHADER_SOURCE ${SHADER_SOURCE_DIR}/${SHADER_FILE})
        # Compute shaders: target Vulkan 1.3 so LocalSize is valid under current
        # maintenance4 / SPIR-V expectations (reduces BestPractices WorkgroupSize noise).
        # vulkan1.1+ for gl_BaseInstance (shaderDrawParameters); compute uses 1.3.
        if(SHADER_EXT STREQUAL ".comp")
            set(SHADER_GLSLC_FLAGS -g --target-env=vulkan1.3)
        else()
            set(SHADER_GLSLC_FLAGS -g --target-env=vulkan1.1)
        endif()
        add_custom_command(
            OUTPUT ${SHADER_SPIRV_OUTPUT}
            COMMAND ${GLSLC} ${SHADER_GLSLC_FLAGS} ${SHADER_SOURCE} -o ${SHADER_SPIRV_OUTPUT}
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling shader: ${SHADER_NAME}"
        )
    endforeach()
    
    # Custom target for all shaders
    set(SHADER_OUTPUT_FILES "")
    foreach(SHADER_FILE ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
        list(APPEND SHADER_OUTPUT_FILES ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    endforeach()
    
    add_custom_target(compile_shaders
        DEPENDS ${SHADER_OUTPUT_FILES}
        COMMENT "All shaders compiled"
    )
endif()

# -----------------------------------------------------------------------------
# Asset / configuration copying
# (Uses OUTPUT + DEPENDS so that *only* changing configuration.json will
#  cause a re-copy on the next `cmake --build`, without needing to touch C++ sources.)
# -----------------------------------------------------------------------------
set(CONFIG_SOURCE ${CMAKE_SOURCE_DIR}/assets/scenes/configuration.json)
set(CONFIG_DEST   ${CMAKE_BINARY_DIR}/assets/scenes/configuration.json)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/assets/scenes)

add_custom_command(
    OUTPUT ${CONFIG_DEST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CONFIG_SOURCE}
            ${CONFIG_DEST}
    DEPENDS ${CONFIG_SOURCE}
    COMMENT "Copying configuration.json to build directory"
)

add_custom_target(copy_configuration ALL
    DEPENDS ${CONFIG_DEST}
    COMMENT "Ensure configuration is up-to-date in build tree"
)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------

# Find Vulkan SDK
# Windows requires explicit help. The stock FindVulkan is sensitive to how CMake
# is invoked (shell vs IDE, generator, whether the env var was present at first configure).
# We seed it using the same $ENV{VULKAN_SDK} that our glslc finder (lines 19-25) already trusts.
if(WIN32)
    if(DEFINED ENV{VULKAN_SDK})
        # Make the whole SDK visible to find modules and any Vulkan*.cmake the SDK ships.
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}")

        # Directly set the two cache variables that FindVulkan populates and caches.
        # FORCE makes the current environment win over any stale build/ cache from a prior run.
        set(Vulkan_INCLUDE_DIR "$ENV{VULKAN_SDK}/Include" CACHE PATH "Vulkan headers" FORCE)
        set(Vulkan_LIBRARY     "$ENV{VULKAN_SDK}/Lib/vulkan-1.lib" CACHE FILEPATH "Vulkan loader" FORCE)
    else()
        message(WARNING "VULKAN_SDK is not set in the environment. Vulkan discovery on Windows will likely fail.")
    endif()
endif()

find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found. Please install Vulkan SDK or set the VULKAN_SDK environment variable.")
else()
    message(STATUS "Vulkan SDK found at: ${Vulkan_INCLUDE_DIRS}")
    message(STATUS "Vulkan library: ${Vulkan_LIBRARY}")
endif()

# Setup FetchContent
include(FetchContent)

# 1. Vulkan Memory Allocator (VMA)
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.3.0
)

# 2. VK-Bootstrap
FetchContent_Declare(
    vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/VK-Bootstrap.git
    GIT_TAG v1.4.341
)

# 3. tinygltf
set(TINYGLTF_HEADER_ONLY ON CACHE INTERNAL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE INTERNAL "" FORCE)
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v3.0.0
)

# 4. GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)

# 5. GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
)

# 6. nlohmann/json (header-only library)
set(JSON_BuildTests OFF CACHE INTERNAL "" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# Enable updating dependencies
set(FETCHCONTENT_QUIET OFF)

# Fetch and make dependencies available
FetchContent_MakeAvailable(VulkanMemoryAllocator vk-bootstrap tinygltf glfw glm nlohmann_json)

# -----------------------------------------------------------------------------
# Build Configuration
# -----------------------------------------------------------------------------

# Platform-specific settings
if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS)
elseif(UNIX)
    # Linux-specific compiler flags
    add_compile_options(-Wall -Wextra -Wpedantic)

    # Add debug symbols for development builds
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_compile_options(-g)
    endif()
endif()

# don't show warnings for dependencies
target_compile_options(VulkanMemoryAllocator INTERFACE -w)
target_compile_options(vk-bootstrap PRIVATE -w)
target_compile_options(tinygltf INTERFACE -w)
target_compile_options(glfw PRIVATE -w)
target_compile_options(glm PRIVATE -w)

add_executable(Aero_Boar_Engine
    src/main.cpp
    src/AeroBoar.cpp
    src/gfx/BufferUtils.cpp
    src/gfx/Engine.Destroy.cpp
    src/gfx/Engine.Initialize.cpp
    src/gfx/Engine.InitializeDevices.cpp
    src/gfx/Engine.InitializePasses.cpp
    src/gfx/Engine.InitializePipeline.cpp
    src/gfx/Engine.InitializeScene.cpp
    src/gfx/Engine.Render.cpp
    src/scene/GltfLoader.cpp
    src/scene/SceneManager.cpp
    src/scene/TransformManager.cpp
    src/scene/Camera.cpp
    src/core/InputManager.cpp
    src/core/Configuration.cpp
    src/gfx/ShaderLoader.cpp
    src/gfx/MaterialManager.cpp
    src/gfx/MeshManager.cpp
    src/gfx/TextureManager.cpp
    src/gfx/IblEnvironment.cpp
    src/gfx/GpuCulling.cpp
    src/gfx/HzbPyramid.cpp
)

# Make sure configuration + (optionally) shaders are available during build.
# The copy_configuration target has proper DEPENDS on the source json,
# so editing only the .json will cause the copy step to run.
add_dependencies(Aero_Boar_Engine copy_configuration)
if(GLSLC_AVAILABLE)
    add_dependencies(Aero_Boar_Engine compile_shaders)
endif()

# Include directories
target_include_directories(Aero_Boar_Engine PRIVATE
    src    
    ${tinygltf_SOURCE_DIR}
    ${GLFW_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
    ${SHADER_OUTPUT_DIR}  # Add this to include compiled shaders
    ${nlohmann_json_SOURCE_DIR}/include
    
)

# Link libraries - use ${Vulkan_LIBRARIES} as fallback if necessary
target_link_libraries(Aero_Boar_Engine PRIVATE 
    Vulkan::Vulkan
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator    
    glfw
    glm::glm
)

# Make Visual Studio debugger use the build directory as the working directory.
# This matches how the project copies configuration.json + shaders into ${CMAKE_BINARY_DIR}/assets and /shaders.
# The engine (and configuration.json) expects to run with CWD at the build root so that
# relative paths like "assets/scenes/..." resolve correctly.
# (On Linux this often "just worked" depending on how you launched the binary.)
set_target_properties(Aero_Boar_Engine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Copy shaders to build directory after compilation (optional, for easier deployment)
if(GLSLC_AVAILABLE)
    add_custom_command(
        TARGET Aero_Boar_Engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory 
        ${SHADER_OUTPUT_DIR} 
        ${CMAKE_BINARY_DIR}/shaders
        COMMENT "Copying compiled shaders to build directory"
    )
endif()