cmake_minimum_required(VERSION 3.5...3.10)

project(Aero_Boar_Engine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -------------------------------------------------------------------------
# Dependencies
# -------------------------------------------------------------------------

# Find Vulkan SDK
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found. Please install Vulkan SDK or set the VULKAN_SDK environment variable.")
else()
    message(STATUS "Vulkan SDK found at: ${Vulkan_INCLUDE_DIRS}")
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
    GIT_TAG v1.4.321
)

# 3. tinygltf
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.7
)

# 4. GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)

# 5. GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 0.9.9.8
)

# Enable updating dependencies
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(VulkanMemoryAllocator vk-bootstrap tinygltf glfw glm)

# -------------------------------------------------------------------------
# Build Configuration
# -------------------------------------------------------------------------

add_executable(hello_world src/main.cpp)

# Include directories
target_include_directories(hello_world PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${tinygltf_SOURCE_DIR}/src
    ${GLM_INCLUDE_DIRS}  # Note: GLM_INCLUDE_DIRS should be used here instead of glm_SOURCE_DIR
)

# Link libraries
target_link_libraries(hello_world PRIVATE
    Vulkan::Vulkan
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    glfw
)

