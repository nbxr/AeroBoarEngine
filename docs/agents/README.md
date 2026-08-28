# Agents / Memory

This directory contains the canonical, shared project knowledge for AeroBoarEngine.

These files are intended to be read by any AI coding agent (Grok Build, Continue.dev, Kilo Code, etc.) working on the project.

## Files

- `project_brief.md` — High-level vision and goals for the project.
- `product_context.md` — Problem being solved and target outcomes.
- `tech_context.md` — Core tech stack, libraries, and coding conventions (incl. optional Tracy, HUD / frame stats).
- `architecture_principles.md` — Key architectural patterns and Quest 3 optimization principles.
- `current_state.md` — Lightweight view of where the project currently stands (what is done, what is in progress, major gaps).
  - The `scene::Camera` desktop controls are documented in detail in `src/scene/Camera.h`.

Architecture plans live under `docs/architecture/` (not only this folder), including:

- `ecs-plan.md` — **near-term** custom ECS, InputFrame, Player + camera
- `desktop-inputs.md` — device input; **accepted** RDP/trackpad look-rail (reopen at camera cleanup)
- `gltf-extensions.md` — Khronos extension support matrix (e.g. AnimationPointerUVs gaps)
- `animation-plan.md` / `physics-plan.md` / `vr-chess-physics-plan.md`
- `visibility-lod-plan.md` — frustum / Hi-Z / Quest GMEM
- `cascadebake-plan.md` — CascadeBake LOD (`CascadeOven`)
- `pipeline-implementation.md`, `lighting-implementation.md`, `game-object-implementation.md`

## Usage

Agents should read the relevant files at the start of non-trivial tasks.

Durable decisions and architectural intent live here. Tool-specific memory (e.g. Grok's native memory or Continue memory banks) may be used for session notes, but should not contradict these files.

When making significant progress or decisions, update the appropriate file(s) in this directory.

These files are considered part of the project’s canonical documentation alongside `AGENTS.md`.