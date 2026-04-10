# Progress

## Completed
- [x] Project structure setup
- [x] Core architecture definition
- [x] Vulkan Initialization (Instance, Device, Swapchain)
- [x] VMA Integration (Memory management)
- [x] Swapchain & Presentation logic
- [x] Basic Pipeline creation (Graphics pipeline, Render passes)
- [x] Descriptor Set & Buffer management
- [x] Material/Pipeline system initialization

## In Progress
- [x] Render Loop implementation
- [x] Resource management (Buffer/Image lifecycle)
- [x] Compute Shader integration (for post-processing)

## Next Steps
- [ ] The implementation of the pre-compute pass remains pending and is divided into the following planned phases:
  - [ ] Phase 1 (Data Structures): Implementation of the Object Data Buffer (AABBs/Metadata) and the VkDrawIndexedIndirectCommand buffer management.
  - [ ] Phase 2 (Compute Shader): Development of the GLSL compute shader for frustum culling and atomic counter management for visibility indices.
  - [ ] Phase 3 (Pipeline & Execution): Implementation of pipeline creation in Engine.InitializePipeline.cpp and the command buffer recording logic (Dispatch -> Buffer Barrier -> Indirect Draw) in Engine.Render.cpp.
