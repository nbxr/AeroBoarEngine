# Active Context

## Current Focus
- **Core Engine Initialization**: Implementing the foundational Vulkan bootstrap sequence and resource intitialization for Linux desktop environment. Note: Android Quest 3 will be implemented in the future after desktop implementation is working.

## Recent Changes
- [x] Finalized Memory Bank setup.

## Immediate Implementation Tasks
- [ ] **Vulkan Instance Creation**:
  - [x] Implement `VkInstance` creation with required extensions (e.g., `VK_KHR_surface`).
  - [x] Implement Linux surface creation with GLFW
- [ ] **Bindless Rendering Data Structures**:
  - [x] Define `core::AllocatedBuffer` for VMA management
  - [x] Define `core::AABB` for bounding box logic
  - [x] Implement `core::MeshSSBO` for geometry data
  - [ ] Define `core::MaterialSSBO` for material data
  - [ ] Implement `core::SceneManager` for instance management
- [ ] **Pre-Compute Pass**:
  - [ ] Setup everything necessary for the pre-compute pass where culling will be executed

## Cleanup Completed
- [x] Memory bank cleaned and project files tidied.

## Blockers / Risks
- **Extension Availability**: Need to verify `VK_EXT_descriptor_indexing` availability on target Quest 3 driver versions during device selection.


