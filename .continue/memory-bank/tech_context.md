# Tech Context: AeroBoarEngine
## Core Tech Stack
- **Language**: C++ (with a preference for C-style architecture: structs and static functions)
- **Graphics API**: Vulkan
- **VR Platform**: Meta Quest 3 (Android-based / aarch64)
- **Memory Management**: VulkanMemoryAllocator (VMA)

## Key Libraries & Extensions
- **Vulkan Extensions**: 
  - `VK_KHR_multiview` (for stereo rendering)
  - `VK_EXT_descriptor_indexing` (for bindless rendering)
- **Math Library**: GLM (OpenGL Mathematics)
- **Android Integration**: `android_native_app_glue` for activity lifecycle management.

## Dependency Management
- **External Libraries**: Use CMake `FetchContent` or Git Submodules (decide on one) to manage VMA and GLM.
- **Build System**: CMake (targeting Android NDK).
- **Toolchain**: Android NDK (latest stable).

## C++ Coding Conventions
This section details the implementation-level style. These patterns complement the architectural rules in `systemPatterns.md`.

- **Architecture** – Prefer C-style structs and static functions over large class hierarchies. This maintains a data-oriented flow compatible with Vulkan.
- **Naming**
  - Classes / Structs: `PascalCase`
  - Namespaces: `lowercase`
  - Functions / Variables / Arguments: `snake_case`
- **Memory Management**
  - All images and buffers **must** be allocated via **VMA**.
  - Use `VMA_ALLOCATION_CREATE_USER_DATA_CAPTURE_EXT` where appropriate.
  - Use `ALLOC_MEMORY_TYPE_TRANSIENT` or `ALLOC_MEMORY_TYPE_LAZILY_ALLOCATED` for attachments to minimize DRAM traffic.
- **Error Handling**
  - All Vulkan API calls returning `VkResult` must be checked.
  - For critical failures (e.g., Device Loss), use a controlled shutdown sequence.
  - For non-critical errors, use the engine's internal logging system (to be implemented).

## Build & Deployment
- **Target Platform**: Android (Quest 3 / aarch64).
- **Optimization Target**: Qualcomm Adreno GPU (optimizing for GMEM/on-chip memory usage).
- **Continuous Integration**: (e.g., "All builds must pass Android NDK Clang-tidy checks").

## Resource Lifecycle & Synchronization

### 1. Frame-in-Flight Pattern
- The engine utilizes a **Double/Triple Buffering** strategy for all per-frame data.
- **Per-Frame Resources**: Any buffer or descriptor set that changes every frame (e.g., Camera UBO, Dynamic Push Constants) must be arrayed by `MAX_FRAMES_IN_FLIGHT`.
- **Synchronization**: Use `VkFence` to ensure the CPU does not overwrite a buffer that is still being read by the GPU from a previous frame.

### 2. Resource Destruction
- **Deferred Destruction**: To avoid destroying resources currently in use by the GPU, all `vkDestroy*` calls for transient resources must be queued and executed only after the associated `VkFence` has been signaled.