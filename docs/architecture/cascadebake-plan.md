# CascadeBake Planning Document
**AeroBoarEngine – Progressive LOD System for Quest 3**  
**Last updated:** 2026-08-13  
**Status:** Design only — not implemented.

**Canonical for:** automatic near→mid→far LOD (meshlets, octahedral impostors, skybox bake), `CascadeOven`, job-system work, static vs dynamic classification.

**Not this doc:** frustum / optional Hi-Z / Adreno prepass policy — `docs/architecture/visibility-lod-plan.md`. Cluster / Nanite-like raster is a later *near-field* path after meshlets; it does not replace CascadeBake’s mid/far stages.

**See also**
- `docs/architecture/visibility-lod-plan.md` — what is culled today; Quest GMEM
- `docs/architecture/ecs-plan.md` — ECS components CascadeOven will read
- `docs/architecture/physics-plan.md` — `KHR_physics_rigid_bodies` motion types
- `docs/architecture/animation-plan.md` — animation = never skybox-bake
- `docs/agents/current_state.md` — implementation snapshot

## Overview

**CascadeBake** is a progressive Level-of-Detail (LOD) system that automatically reduces the rendering cost of distant geometry:

1. **Near** → Full meshlets (generated with meshoptimizer)
2. **Mid** → Impostors (preferably octahedral)
3. **Far / Background** → Baked into the skybox (permanent, low-cost)

The system is designed to run efficiently on **Quest 3** (Snapdragon XR2 Gen 2) and leans heavily on a custom **job system / multithreading**.

### Key Names
- **System name:** CascadeBake
- **Managing class:** CascadeOven

---

## Goals

- Dramatically reduce draw cost and memory for distant objects.
- Keep the main/render thread free (all heavy work goes through the job system).
- Be as automatic as possible — no manual per-object “bake” flags required in Blender.
- Support both static and dynamic entities correctly.
- Stay compatible with the emerging Khronos physics extensions (`KHR_physics_rigid_bodies`).

---

## Automatic Classification (No Explicit Authoring)

CascadeOven classifies entities at load / spawn time using existing ECS data:

| Condition                                      | Classification | Allowed Stages                  | Notes |
|------------------------------------------------|----------------|----------------------------------|-------|
| Has active Animation component                 | **Dynamic**    | Meshlets → Impostor only        | Never bake to skybox |
| RigidBody type == **Static**                   | **Static**     | Meshlets → Impostor → Skybox    | Full cascade eligible |
| RigidBody type == **Kinematic / Passive**      | **Movable**    | Meshlets → Impostor only        | Code/animation driven |
| RigidBody type == **Dynamic**                  | **Dynamic**    | Meshlets → Impostor only        | Fully simulated |
| No RigidBody **and** no Animation              | **Static**     | Meshlets → Impostor → Skybox    | Safe default for pure visual props |
| Parent is Dynamic / Movable                    | Inherit        | Same as parent                  | Prevent baking children of movers |

### Additional Rules
- **Sleeping Dynamic bodies**: Can freeze impostor updates when far + inactive, but still never promote to skybox.
- **Hysteresis**: Once an object starts the bake path, require a clear signal (rigid-body type change or animation start) before demoting.
- Classification is primarily done once at creation, then re-evaluated only when rigid-body type or animation state changes.

This model aligns with Blender rigid-body types and the Khronos `KHR_physics_rigid_bodies` extension (Static / Kinematic / Dynamic).

---

## Pipeline Stages

### 1. Meshlet Generation (Near)
- Use **meshoptimizer** (`meshopt_buildMeshlets` preferred, or `meshopt_buildMeshletsScan` for speed).
- Typical limits: 64 vertices / 124–126 triangles.
- Generated at load time (or on demand) via the job system.
- Output stored for use by mesh shaders or regular rendering path.

### 2. Impostor Generation (Mid)
- Preferred: **Octahedral** (or hemi-octahedral) impostors.
- Fallback: simpler view-dependent billboards.
- Capture albedo + depth (normals optional).
- Generation is multi-view and must be jobified / spread across frames when necessary.
- Update frequency depends on classification (static objects freeze sooner).

### 3. Skybox Bake (Far / Background)
- Only **Static** objects that have been beyond a far threshold for sufficient time.
- Render contribution into cubemap faces (low resolution, e.g. 512–1024).
- After successful bake, free meshlets + impostor data.
- Expensive and infrequent — treat as essentially permanent.

---

## Job System Requirements

CascadeBake is built around a custom job system:

- Work-stealing or multi-queue design.
- Support for dependencies / continuations.
- Priorities (frame-critical vs background LOD work).
- Cancellation support (object destroyed, classification changes, player approaches).
- Time budgeting so background work never causes frame hitches.
- Safe sharing of Vulkan resources (command buffers, temporary render targets, fences/timeline semaphores).

**Example job flow for a Static object:**
```
Load glTF → Generate Meshlets → (distance threshold) Generate Impostor Atlas → (far + time) Bake Skybox Faces → Free heavy data
```

All stages above should be schedulable as jobs.

---

## Quest 3 Constraints & Guidelines

- Memory budget is tight — prefer low-resolution impostor atlases and infrequent skybox updates.
- Spread multi-view impostor generation and cubemap face rendering across frames or background queues.
- Keep worker thread count modest (avoid fighting the OS and thermal limits).
- Prefer mono impostors + parallax/depth correction over full stereo pairs unless quality demands otherwise.
- Aggressive culling and prioritization: only generate/update what is actually visible and will stay relevant.

---

## ECS Integration Notes

- Custom ECS.
- Rigid body type and Animation components are the primary signals for classification.
- CascadeOven queries these components (and parent relationships) to decide stage eligibility.
- Optional: store a small `CascadeState` component on entities that tracks current stage, generation version, freeze flags, etc.

---

## Open Questions / Next Steps

1. Exact distance thresholds and hysteresis timings.
2. Impostor atlas resolution and view-grid size for Quest 3 sweet spot.
3. Whether skybox bake should be a full cubemap replace or a layered/compositable contribution.
4. Concrete job system API surface and how CascadeOven will schedule work.
5. Handling of instanced / shared meshes during impostor and bake stages.
6. Debug visualization (current stage, freeze state, pending jobs).

---

## Naming Summary

| Item              | Name          |
|-------------------|---------------|
| System            | CascadeBake   |
| Managing Class    | CascadeOven   |
| Fun internal name | Smusher       |

---

*This document is intended as a living planning reference for Grok Build and ongoing implementation in AeroBoarEngine.*
