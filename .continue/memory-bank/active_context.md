# Active Context

## Current Focus
- **Core Engine Initialization**: Implementing the foundational Vulkan bootstrap sequence.
## Recent Changes
- [x] Finalized Memory Bank setup.
- [x] Established architectural and coding standards in `systemPatterns.ram` and `techContext.md`.

## Immediate Implementation Tasks
- [ ] **Vulkan Instance Creation**:
  - [ ] Setup `VkApplicationInfo`.
  - [ ] Implement `VkInstance` creation with required extensions (e.g., `VK_KHR_surface`).
  - [ ] Implement Android surface extension support.
- [ ] **Vulkan Device Setup**:
  - [ ] Physical Device enumeration and selection (targeting Adreno GPU features).
  - [ ] Logical Device creation with `VK_KHR_multiview` enabled.
  - [ ] Queue family discovery (Graphics and Present queues).
- [ ] **VMA Integration**:
  - [ ] Initialize `VmaAllocator` during the device creation sequence.

## Blockers / Risks
- **Android Surface Complexity**: Need to ensure `android_native_app_glue` is correctly providing the `ANativeWindow` for the swapchain.
- **Extension Availability**: Need to verify `VK_EXT_descriptor_indexing` availability on target Quest 3 driver versions during device selection.

