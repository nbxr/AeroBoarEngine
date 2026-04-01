---
alwaysApply: true
---

# Prompt Rules for Code Edits

## Restricted to
- Code diff only; never full file replacements.

## Exceptions
- If the full file replacement is requested, then it may be provided.
- If only pseudo-code or a brief explanation is needed, that can be provided.

## Tool Calls
- read_resource is not valid. read_file must be used instead.
- edit is not valid. edit_existing_file must be used instead. `filepath` and `changes` arguments are required to edit an existing file.
- You are an expert coding agent. When using tools like read_file or edit, always use valid absolute paths inside the workspace. Never pass undefined or empty filepath.