# Tech Context

## Core Tech Stack
- **Language**: C++ (with a preference for C-style architecture)
- **Graphics API**: Vulkan
- **VR Platform**: Meta Quest 3 (Android-based)
- **Memory Management**: VulkanMemoryAllocator (VMA)

## Key Libraries & Extensions
- **Vulkan Extensions**: 
  - `VK_KHR_multiview` (for stereo rendering)
  - Descriptor Indexing / Bindless rendering
  - `VK_EXT_descriptor_indexing`
- **Android/Quest 3 Specifics**:
  - Optimized for Qualcomm Adreno GPU (GMEM usage)
  - Focus on reducing DRAM traffic

## Build System & Toolchain
- **Build System**: CMake
- **Target Platform**: Android (Quest 3 / aarch64)
- **Toolchain**: Android NDK (latest stable)
- **Key Requirement**: Must support `android_native_app_glue` for activity lifecycle management.

## C++ Coding Conventions (language‑specific)
This section details the **language‑level style** that the team agreed to follow.
The patterns here are implementation‑level and complement the architectural
rules listed in `systemPatterns.md`.

- **Architecture** – Prefer C‑style structs and static functions over large class hierarchies.  This keeps compilation fast on the Quest 3 and matches the Vulkan‑driven data flow.
- **Naming**
  - Classes / Structs: `PascalCase`
  - Namespaces: `lowercase`
  - Fields / Methods / Arguments: `snake_case`
- **Memory** – All images and buffers must be allocated through the **VMA** layer using either `ALLOC_MEMORY_TYPE_TRANSIENT` or `ALLOC_MEMORY_TYPE_LAZILY_ALLOCATED`.  This ensures minimal DRAM traffic and matches the `Transient Attachments` rule in `systemPatterns.md`.
