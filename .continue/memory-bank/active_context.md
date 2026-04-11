# Active Context

## Current Focus
- **Core Engine Initialization**: Implementing the foundational Vulkan bootstrap sequence and resource intitialization for Linux desktop environment. Note: Android Quest 3 will be implemented in the future after desktop implementation is working.

## Recent Changes
- [x] Finalized Memory Bank setup.

## Immediate Implementation Tasks
- [ ] **Vulkan Instance Creation**:
  - [ ] Implement `VkInstance` creation with required extensions (e.g., `VK_KHR_surface`).
  - [ ] Implement Linux surface creation with GLFW
- [ ] **VMA Integration**:
  - [ ] Use `VmaAllocator` for creating buffers necessary for passes
- [ ] **Pre-Compute Pass**:
  - [ ] Setup everything necessary for the pre-compute pass where culling will be executed

## Blockers / Risks
- **Extension Availability**: Need to verify `VK_EXT_descriptor_indexing` availability on target Quest 3 driver versions during device selection.
