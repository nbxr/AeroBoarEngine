# AeroBoarEngine — Project Rules for AI Agents

This file is the primary contract for all AI coding agents (Grok Build, Continue.dev, Kilo Code, and future tools) working on this project.

## 1. Core Project Information

**Project**: High-performance Vulkan rendering engine optimized for Meta Quest 3 (with Linux desktop development mode).

**Key Constraints**: TBDR mobile GPU (Adreno 740). Minimize CPU overhead and DRAM traffic. Prefer on-chip (GMEM) work. Data-oriented C++ design.

**Canonical Documentation** (read these for architecture and planning work):

- `docs/agents/` — Current state, architecture principles, tech context, and project knowledge
- `docs/project-plan.md` — Goals and roadmap
- `docs/architecture/pipeline-implementation.md`
- `docs/architecture/game-object-implementation.md`

When making lasting decisions, update the documentation above rather than tool-private memory.

## 2. Coding Conventions

- **Style**: Data-oriented. Prefer plain structs and composition over deep inheritance.
- **Naming**: `PascalCase` for types/structs, `snake_case` for functions and variables.
- **Namespaces**:
  - `core` — Small set of universal utilities (`core::Handle`, `core::AABB`, and future non-rendering primitives).
  - `gfx` — Everything graphics/Vulkan: RHI wrappers, resource managers & bindless data, contexts, pipelines, and rendering ownership (`gfx::Renderer`, `Allocated*`, managers, GPU layouts, etc.).
  - `scene` — High-level game object model, transforms, and loading that produces data for the gfx layer (`scene::SceneManager`, `scene::GltfLoader`, `scene::GameObject`/`RenderMesh`, current flat `SceneInstance`).

  The split aligns with the two architecture documents in `docs/architecture/` (pipeline vs. game-object concerns).
- All GPU memory is allocated via **VMA**.
- Prefer transient/lazily allocated images for MSAA targets.
- Use `VK_ATTACHMENT_STORE_OP_DONT_CARE` aggressively.
- Keep CPU-side data lightweight and cache-friendly.

## 3. Build & Development

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

- Requires Vulkan SDK (headers + `glslc`).
- Dependencies are fetched automatically via `FetchContent` on first configure.
- Shaders in `shaders/` are compiled via custom CMake target when `glslc` is found.
- **Future:** switch shader compilation to **glslang** when cross-platform (Quest/Android) development starts; see `docs/agents/tech_context.md`.
- The binary is `Aero_Boar_Engine`.

### Running
The executable currently opens a desktop GLFW window. It loads the scene defined in `assets/scenes/configuration.json`.

**Important**: Scene loading reads `assets/scenes/configuration.json`.
- `defaultScene` selects the entry from `scenes[]` by name.
- `activeSystem` picks which entry in the `home[]` array to use.
- The resolved path is `home[activeSystem].path` concatenated with the chosen scene's `filename` (relative).
Update the `home` paths for your platform(s) before running. The `scenes[].filename` values are portable relative paths under the chosen home.

### Important Notes
- A basic render loop (`Engine::render()`) draws glTF scenes with bindless PBR, GPU frustum/Hi-Z cull, and multi-draw indirect.
- OpenXR / multiview / Quest packaging and reverse-Z are still future work — see `docs/agents/current_state.md`.
- Check `docs/agents/current_state.md` for the latest implementation status and focus areas.

## 4. Instructions for AI Agents

**At the start of any non-trivial task**, read:
- This `AGENTS.md`
- Relevant files in `docs/agents/` (especially `current_state.md` and `architecture_principles.md`)
- The architecture documents listed in section 1

**General Rules**:
- Prefer editing canonical documentation (`docs/`) for architectural decisions rather than tool-private memory.
- When the task is complete, suggest updates to the relevant docs if the change has lasting impact.
- Use `TODO` comments in code for work intentionally left for later.
- Keep changes focused. Do not refactor unrelated areas "while you're here."

**Grok Build**: Use the `/plan` or plan-mode capability for larger changes.

**Continue.dev Specific**:
- Tool-specific state (such as `.continue/` memory banks or rules) should be considered secondary.
- These files should **point to** `AGENTS.md` + `docs/agents/` as the primary source of truth.
- Use tool-private memory only for transient session notes. Durable project knowledge belongs in `docs/agents/`.

**Kilo Code Specific**:
- Worktrees created by Kilo should live **outside** the main repository tree when possible (e.g. `~/kilo-worktrees/...`).
- Do not rely on files inside `.kilo/worktrees/` for long-term state.

## 5. Documentation & Memory Hygiene

**Single source of truth hierarchy** (in order of authority):
1. `AGENTS.md` (this file) + `docs/agents/` + architecture documents
2. Code (the implementation is the ultimate test)
3. Tool-private memory (e.g. Continue memory banks, Kilo plans, or Grok’s `~/.grok/memory/`)

Never let tool-specific memory become the only place important context lives.
Periodically review and prune tool memory banks against the canonical docs.

## 6. Current State

See `docs/agents/current_state.md` for the latest implementation status, focus areas, and known gaps.

This file is intentionally kept concise. Maintain it so any agent can quickly understand the project without relying on tool-specific memory.
