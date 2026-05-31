# Agents / Memory

This directory contains the canonical, shared project knowledge for AeroBoarEngine.

These files are intended to be read by any AI coding agent (Grok Build, Continue.dev, Kilo Code, etc.) working on the project.

## Files

- `project_brief.md` — High-level vision and goals for the project.
- `product_context.md` — Problem being solved and target outcomes.
- `tech_context.md` — Core tech stack, libraries, and coding conventions.
- `architecture_principles.md` — Key architectural patterns and Quest 3 optimization principles.
- `current_state.md` — Lightweight view of where the project currently stands (what is done, what is in progress, major gaps).
  - The `scene::Camera` desktop controls are documented in detail in `src/scene/Camera.h`.

## Usage

Agents should read the relevant files at the start of non-trivial tasks.

Durable decisions and architectural intent live here. Tool-specific memory (e.g. Grok's native memory or Continue memory banks) may be used for session notes, but should not contradict these files.

When making significant progress or decisions, update the appropriate file(s) in this directory.

These files are considered part of the project’s canonical documentation alongside `AGENTS.md`.