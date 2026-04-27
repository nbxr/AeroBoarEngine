# Progress

## Completed
- [x] Project structure setup
- [x] Core architecture definition
- [x] Vulkan Initialization (Instance, Device, Swapchain)
- [x] VMA Integration (Memory management)
- [x] Swapchain & Presentation logic
- [x] Basic Pipeline creation (Graphics pipeline, Render passes)
- [x] CMakeLists.txt shader compilation with glslc
- [x] Cleanup memory bank and project files
- [x] GLTF Model Loading (CPU-side):
  - [x] `core::GltfLoader` implementation (parsing & extraction)
  - [x] `core::MeshData` structure for intermediate CPU storage
- [x] Scene Loading Pipeline:
  - [x] `Engine::load_default_scene()` reads from `configuration.json`
  - [x] `Engine::load_scene()` orchestrates GLTF loading, mesh/material extraction
  - [x] `Engine::cleanup_scene()` stub for cleanup
- [x] TextureManager: Staged texture loading with `TextureID` handles
- [x] MeshManager: Staged mesh upload pipeline with `MeshPrimitiveID` handles

## In Progress
- [ ] MaterialManager: Full material system with GPU SSBO upload, double buffering
- [ ] Bindless Rendering Data Structures
  - [ ] `core::MeshSSBO` (Geometry)
  - [ ] `core::MaterialSSBO` (Materials)
  - [ ] `core::SceneManager` (Instance Management)
- [ ] GPU Upload & Buffer Management (Next immediate step)
- [ ] Render Loop implementation
- [ ] Compute Shader integration (for culling)

## Next Steps
- [ ] **Phase: GPU Mesh Upload & Render Loop Integration**
  - [ ] Create `core::GpuMesh` class to manage Vulkan buffers (VMA) for vertex/index data.
  - [ ] Implement CPU-to-GPU data upload from `core::MeshData` to `VkBuffer`.
  - [ ] Update `core::SceneManager` to instantiate `GpuMesh` objects.
  - [ ] Implement indirect draw calls using the updated bindless descriptor set.
- [ ] The implementation of the pre-compute pass (Compute Shader) remains pending for subsequent phases.

