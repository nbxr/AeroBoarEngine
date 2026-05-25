# Game Object Architecture

## Overview
Lightweight, cache-friendly data structures designed for a C++ Vulkan engine using GPU-driven rendering on Meta Quest 3. Focuses on efficient GLTF loading, multi-mesh / multi-material support (e.g. characters), and direct feeding into compute-based culling and indirect draw pipelines.

## Status
**Target architecture** (not yet fully implemented).  
The current scene system uses a simpler flat `SceneInstance` model with embedded `mat4` transforms. The higher-level `GameObject` / `RenderMesh` + `TransformManager` SOA design described below is the planned evolution for skinned, multi-part, and instanced objects.

## Core Principles
- Flat SOA-style layouts for high cache efficiency and low CPU overhead.
- One `RenderMesh` entry maps 1:1 to a draw / mesh-shader task.
- Global bindless storage for meshes, materials, and textures.
- Separate high-level ownership from low-level render data.
- Minimal CPU-side hierarchy; most work (culling, transform propagation, skinning) pushed to GPU.

## Key Structures

### GameObject
High-level entity container.  
Holds root transform, shared skinning data (if any), and game-relevant logic.  
One GameObject can own many `RenderMesh` entries (body + armor + weapon, etc.).

### RenderMesh
Thin render descriptor.  
Contains mesh reference, material reference, final transform index for culling, optional skin index, and local AABB.  
Supports both rigid and skinned pieces.

### Supporting Data
- **AABB**: Simple min/max bounds (mesh-local, expanded to world for culling).
- **Transform SOA**: Dense array of position, rotation, scale (or matrices).
- **Material**: Global SSBO with texture indices (albedo, normal, MR, etc.) and PBR factors.
- **MeshStorage**: Global GPU buffers holding all vertex data (future meshlets).

## Management Systems

### TransformManager
Manages a dense, contiguous array of transforms in SOA format.
- Provides `transform_index` used by `GameObject` and `RenderMesh`.
- Handles CPU-side updates and efficient GPU buffer uploads (SSBO).

### SceneManager (The Glue)
Orchestrates the lifecycle of all renderable data.
- **Registry**: Tracks all active `GameObject` and `RenderMesh` instances.
- **Buffer Orchestration**: Aggregates `RenderMesh` and `Transform` data into GPU-ready SSBOs for the compute culling pass.
- **Resource Lifecycle**: Coordinates with `GltfLoader` to populate `MeshStorage` and `MaterialManager`.

## GLTF Loading Flow
1. **Load**: `GltfLoader` parses file into `tinygltf::Model`.
2. **Register**: `SceneManager` iterates nodes, creating `GameObject` and `RenderMesh` entries.
3. **Populate**:
   - Meshes $\to$ `MeshStorage`.
   - Materials/Textures $\to$ Bindless Array + `MaterialManager`.
   - Transforms $\to$ `TransformManager` (SOA).
4. **Finalize**: `SceneManager` packs all `RenderMesh` descriptors into a single GPU-resident buffer for the culling pipeline.

## Handling Complex Objects
- Characters / multi-part models $\to$ single `GameObject` owning multiple `RenderMesh` entries.
- Shared skeleton via common `skin_index`.
- Rigid attachments (weapons, props) use bone-derived transforms copied into their `transform_index`.
- Multi-material meshes naturally split into separate `RenderMesh` entries.

## Rendering Pipeline Fit
- `RenderMesh` list feeds GPU culling compute (frustum / occlusion).
- Surviving items build indirect draw buffer or task shader input.
- Skinning applied in mesh/vertex shader using joint matrices.
- Optimized for Quest 3: minimal bandwidth, few render passes, bindless resources.

## Potential Friction Points to Monitor
- Ensure that the "Buffer Orchestration" in the SceneManager only uploads changed transforms (dirty flagging) to prevent pipeline stalls.
- Ensure that when new GameObjects are spawned, they can update the bindless SSBO/Descriptor array without invalidating the bound descriptors mid-frame (usually handled by double-buffering descriptor sets or using `VK_EXT_descriptor_update_template`).