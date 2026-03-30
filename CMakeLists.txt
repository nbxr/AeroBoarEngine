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

# Enable updating dependencies
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(VulkanMemoryAllocator vk-bootstrap tinygltf glfw glm)

# -------------------------------------------------------------------------
# Build Configuration
# -------------------------------------------------------------------------

# Windows-specific settings
if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_DEPRECATE -D_CRT_SECURE_NO_WARNINGS)
endif()

add_executable(Aero_Boar_Engine 
    src/main.cpp 
    src/AeroBoar.cpp
    src/core/Engine.Destroy.cpp
    src/core/Engine.Initialize.cpp
    src/core/Engine.InitializeDevices.cpp
    src/core/Engine.InitializePasses.cpp
    src/core/Engine.InitializePipeline.cpp    
    src/core/Engine.Render.cpp    
    src/core/MaterialManager.cpp
)

# Include directories
target_include_directories(Aero_Boar_Engine PRIVATE
    ${tinygltf_SOURCE_DIR}/src
    ${GLFW_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(Aero_Boar_Engine PRIVATE 
    ${Vulkan_LIBRARIES}
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator    
    glfw
    glm::glm
)