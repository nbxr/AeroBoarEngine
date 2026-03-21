# CMake minimum version required
cmake_minimum_required(VERSION 3.50)
project(AeroBoarEngine)

# Enable C++20 or later
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add an executable
add_executable(${PROJECT_NAME} src/main.cpp)

# Set the VULKAN_SDK environment variable to point to your Vulkan SDK installation path
if (NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "Vulkan SDK not found! Define the VULKAN_SDK environment variable.")
endif()
set(VULKAN_SDK $ENV{VULKAN_SDK})

# Include directories for Vulkan and its dependencies
include_directories(${VULKAN_SDK}/include)

# Add Vulkan libraries if not already added by FetchContent
if(NOT TARGET Vulkan::Vulkan)
    find_library(VulkanSDK_LIBRARIES NAMES vulkan HINTS ${VULKAN_SDK}/Lib)
    find_path(VulkanSDK_INCLUDES NAMES vk_platform.h HINTS ${VULKAN_SDK}/Include/vulkan)
    
    # Check if we found the includes
    if(NOT VulkanSDK_INCLUDES)
        message(FATAL_ERROR "Vulkan SDK include directory not found at ${VULKAN_SDK}/include")
    endif()
    
    add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
    set_target_properties(Vulkan::Vulkan PROPERTIES
        IMPORTED_LOCATION "${VulkanSDK_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${VulkanSDK_INCLUDES}")
endif()

# FetchContent for dependencies
include(FetchContent)

# GLFW
FetchContent_Declare(
    GLFW
    URL https://github.com/glfw/glfw/archive/refs/tags/3.3.8.zip
)
FetchContent_GetProperties(GLFW)
if (NOT GLFW_POPULATED)
    FetchContent_MakeAvailable(GLFW)
endif()

# Jolt Physics
FetchContent_Declare(
    JoltPhysics
    URL https://github.com/jrouwe/JoltPhysics/archive/refs/heads/master.zip
)
FetchContent_GetProperties(JoltPhysics)
if (NOT JoltPhysics_POPULATED)
    FetchContent_MakeAvailable(JoltPhysics)
endif()

# OpenAL Soft - Set minimum CMake version for OpenAL
set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
FetchContent_Declare(
    OpenAL
    URL https://github.com/kcat/openal-soft/archive/refs/tags/1.22.0.zip
)
FetchContent_GetProperties(OpenAL)
if (NOT OpenAL_POPULATED)
    FetchContent_MakeAvailable(OpenAL)
endif()

# Link libraries to the target
target_link_libraries(${PROJECT_NAME} PUBLIC 
    Vulkan::Vulkan 
    GLFW::GLFW 
    OpenAL::openal 
    JoltPhysics
)