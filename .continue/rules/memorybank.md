---
description: Enforce persistent project memory using 
   the ./.continue/memory-bank/ folder. Read it first
   and update it ruthlessly.
alwaysApply: true
---

You have a **Memory Bank** — your external, persistent brain for this project. It lives in the `./.continue/memory-bank/` directory at the project root.

**MANDATORY PROTOCOL — NEVER SKIP THIS:**

1. At the **very start of every single task, chat, edit, or agent run**, you **MUST** read and fully internalize ALL files in the `.continue/memory-bank/` directory. This is non-negotiable. Confirm you've finished internalizing the memory bank once complete.

2. The core files (all will exist) are:
   - `./.continue./memory-bank/active_context.md` — What's active right now, recent changes, current focus
   - `./.continue./memory-bank/product_context.md` — Why the project exists, user problems, UX goals
   - `./.continue./memory-bank/progress.md` — What's done, what's broken, known issues, next steps
   - `./.continue./memory-bank/project_brief.md` — Foundation vision, goals, and requirements
   - `./.continue./memory-bank/system_patterns.md` — Architecture, design patterns, component relationships
   - `./.continue./memory-bank/tech_context.md` — Tech stack, libraries, quirks, conventions

3. Treat the entire Memory Bank as **sacred canon**. Never contradict it. If something in the bank is outdated, update the relevant file(s) immediately and note the change.

4. After every meaningful action (completing a task, fixing something, making a decision), **update the Memory Bank**:
   - Keep `activeContext.md` and `progress.md` fresh.
   - Add new patterns or tech notes where they belong.
   - Be concise but precise.
   - Prune old and irrelevant content, especially  from `progress.md`.

5. When the user references the Memory Bank, or when context feels fuzzy, explicitly reaffirm you've read the latest version.

This Memory Bank is the single source of truth. Use it to stay consistent, remember decisions, and never hallucinate old shit. Keep files lean — no fluff.

Now go be a terrifyingly consistent coding gremlin.