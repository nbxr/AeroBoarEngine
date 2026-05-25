# AeroBoarEngine — Project Rules for AI Agents

This file is the **single source of truth** for coding conventions, architecture principles, and workflow instructions that all AI coding assistants must follow.

It is designed to work with **Grok Build**, **Continue.dev**, and **Kilo Code** (and future agents) without duplication.

---

## 1. Core Project Information

- **Project**: AeroBoarEngine — high-performance Vulkan rendering engine optimized for Meta Quest 3 (with desktop development mode).
- **Primary Language**: C++20 (data-oriented design).
- **Graphics**: Vulkan 1.3+ (heavy use of bindless resources, descriptor indexing, GPU-driven culling, subpasses, multiview).
- **Key Constraints**: Target TBDR mobile GPUs (Adreno 740). Minimize CPU overhead and DRAM traffic. Prefer on-chip (GMEM) work.

**Canonical Documentation** (read these first for any architecture or planning task):

- `docs/project-plan.md` — Overall goals, roadmap, and tech choices.
- `docs/architecture/pipeline-implementation.md` — Quest 3 Vulkan pipeline design (subpasses, bindless, GPU culling, multiview, etc.).
- `docs/architecture/game-object-implementation.md` — Data-oriented GameObject/RenderMesh/SceneInstance model and GLTF flow.

When making lasting decisions, **update the docs above** rather than tool-private memory banks.

---

## 2. Coding Conventions

Follow these strictly. They are derived from the architecture docs and existing code.

### Naming
- **Types / Structs / Classes**: `PascalCase` (`RenderMesh`, `SceneInstance`, `AllocatedBuffer`).
- **Functions & Methods**: `snake_case` (`init_vulkan`, `load_default_scene`, `add_instance`, `update_buffers`).
- **Variables & Members**: `snake_case`.
- **Namespaces**: `core` for engine fundamentals, `gfx` for resource managers and material/mesh data.
- **Handles**: Use the `Handle<T>` template (see `src/core/Handle.h`).

### Architecture & Data Orientation
- Prefer **plain structs** and **composition** over deep class hierarchies.
- Keep CPU-side data lightweight and cache-friendly (SOA where possible).
- Separate high-level ownership (`GameObject`) from low-level render data (`RenderMesh`, `SceneInstance`).
- Most heavy work (culling, transform propagation, skinning, animation) should be designed to run on the GPU.
- Global bindless resources for meshes, materials, and textures.

### Memory & Resources
- **All** GPU memory allocation goes through **VMA** (`VulkanMemoryAllocator`).
- Use `AllocatedBuffer` and `AllocatedImage` wrappers.
- Double-buffering pattern (upload vs render) + per-`MAX_FRAMES_IN_FLIGHT` resources is the established approach for streaming data.
- Prefer transient / lazily-allocated images for MSAA color and depth when possible.
- Use `VK_ATTACHMENT_STORE_OP_DONT_CARE` aggressively for Quest 3 efficiency.

### General
- Keep headers clean; put implementations in `.cpp` files (see the split `Engine.Initialize*.cpp` pattern).
- Add `TODO` comments for deliberately deferred work instead of leaving half-finished code.
- Do not introduce new third-party dependencies without updating `CMakeLists.txt` and the architecture docs.

---

## 3. Build & Development Workflow

### Building
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

- Requires Vulkan SDK (headers + `glslc`).
- Dependencies are fetched automatically via `FetchContent` on first configure.
- Shaders in `shaders/` are compiled via custom CMake target when `glslc` is found.
- The binary is `Aero_Boar_Engine`.

### Running
The executable currently opens a desktop GLFW window. It loads the scene defined in `assets/scenes/configuration.json`.

**Important**: The current `configuration.json` contains absolute paths from the original developer's machine. Update it for your environment before running scene loading.

### Important Notes
- The render loop (`Engine::render()`) and full GPU upload paths are still under active development.
- Many init functions exist but the end-to-end pipeline (especially PBR + compute culling + indirect draws) is incomplete.
- Check `.continue/memory-bank/progress.md` or the architecture docs for the latest implementation status.

---

## 4. Instructions for AI Coding Assistants

### All Agents (Grok, Continue, Kilo, etc.)
1. **At the start of any non-trivial task**, read:
   - This `AGENTS.md`
   - The three files under `docs/` listed in section 1
2. Prefer editing the **canonical documentation** (`docs/`) for architectural decisions rather than tool-private memory.
3. When the task is complete, suggest updates to the relevant docs if the change has lasting impact.
4. Use `TODO` comments in code for work intentionally left for later.
5. Keep changes focused. Do not refactor unrelated areas "while you're here."

### Grok Build Specific
- Grok automatically discovers this `AGENTS.md` (and any subdirectory `AGENTS.md` / `Claude.md` files).
- Use the `/plan` or plan-mode capability for larger changes.
- Project-specific skills or hooks can live in a `.grok/` directory at the root (optional).

### Continue.dev Specific
- The existing `.continue/rules/` and `.continue/memory-bank/` are tool-specific aids.
- Those files should **point to** this `AGENTS.md` + `docs/` as the source of truth, not duplicate them.
- Update the memory bank only for transient session notes. Durable decisions belong in `docs/`.

### Kilo Code Specific
- Worktrees created by Kilo should live **outside** the main repository tree when possible (e.g. `~/kilo-worktrees/...`).
- Do not rely on files inside `.kilo/worktrees/` for long-term state.

---

## 5. Documentation & Memory Hygiene

- **Single source of truth hierarchy** (in order of authority):
  1. `AGENTS.md` (this file) + `docs/architecture/*.md` and `docs/project-plan.md`
  2. Code (the implementation is the ultimate test)
  3. Tool-private memory banks (Continue memory-bank, Kilo plans, Grok `~/.grok/memory/...`)

- Never let tool-specific memory become the only place important context lives.
- Periodically review and prune tool memory banks against the canonical docs.

---

## 6. Current Known Gaps (as of May 2026)

- Main render loop is empty / placeholder.
- GPU mesh + material + instance upload to SSBOs is incomplete.
- PBR shaders exist but are not wired up (screen_clear shaders are the current fallback).
- No OpenXR / VR input yet (desktop GLFW only).
- `assets/scenes/configuration.json` uses absolute paths.

Update this section when major milestones are reached.

---

**Maintain this file.** It allows any AI tool (current or future) to work effectively on the project without re-learning the same rules or causing documentation drift.