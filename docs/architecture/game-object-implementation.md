# Game Object Architecture

## Overview
Lightweight, cache-friendly data structures designed for a C++ Vulkan engine using GPU-driven rendering on Meta Quest 3. Focuses on efficient GLTF loading, multi-mesh / multi-material support (e.g. characters), and direct feeding into compute-based culling and indirect draw pipelines.

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
- **MeshStorage**: Global GPU buffers holding all vertex data / meshlets.

## GLTF Loading Flow
- Load unique meshes → global MeshStorage.
- Load unique materials + textures → bindless array + Material SSBO.
- For each primitive: create one `RenderMesh` (set mesh/material indices).
- Group under one `GameObject` for logical objects/characters.
- Build hierarchy and compute initial world transforms / AABBs.

## Handling Complex Objects
- Characters / multi-part models → single `GameObject` owning multiple `RenderMesh` entries.
- Shared skeleton via common `skin_index`.
- Rigid attachments (weapons, props) use bone-derived transforms copied into their `transform_index`.
- Multi-material meshes naturally split into separate `RenderMesh` entries.

## Rendering Pipeline Fit
- `RenderMesh` list feeds GPU culling compute (frustum / occlusion).
- Surviving items build indirect draw buffer or task shader input.
- Skinning applied in mesh/vertex shader using joint matrices.
- Optimized for Quest 3: minimal bandwidth, few render passes, bindless resources.

This architecture keeps CPU light, maximizes GPU parallelism, and scales to complex GLTF scenes with many sub-meshes while staying performant on mobile VR hardware.