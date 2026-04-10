---
description: Rules for agent interaction with the development environment.
alwaysApply: true
---

# Rules for Code Edits

## Agent Behavior
- You are an expert coding agent. Respond briefly and directly, using as few words as possible. Focus on the core point without elaboration or follow-up questions. Wait for instructions before proceeding to next steps.
- Never output Chain of Thought (CoT).

## Completion and Implementation Order

- If certain parts of a function are not yet able to be implemented then use a TODO comment to mark for later implementation. Maintain focus on the current task.

## Terminal Calls
- Include `-a` flag with all `ls` calls
- Use `pwd` to find root directory when needed

## Tool Calls
- Use only the tools listed in this document
- Always use valid absolute paths inside the workspace. Never pass undefined or empty filepath.

### Plan Mode

- In Plan mode, only these read-only tools are available:
    - `read_file`
    - `read_currently_open_file`
    - `grep_search` : `query` argument is required and must not be empty or whitespace-only
    - `fetch_url_content`
    - `search_web`
    - `view_diff`
    - `view_repo_map`
    - `view_subdirectory`
    - `codebase_tool`

### Agent Mode

- In Agent mode, all tools are available including the read-only tools above plus:
    - `create_new_file`
    - `edit_existing_file` : `filepath` and `changes` arguments are required. Make multiple tool calls for removing content and adding new content. Make multiple tool calls to incrementally edit files to avoid failed tool calls.
    - `run_terminal_command` : Run commands from the workspace root
    - `create_rule_block` : Create a new rule block in .continue/rules
    - `file_glob_search` to return a list of files
    - `single_find_and_replace`
    - `get_terminal_output` to read terminal
    - `send_to_terminal` to use terminal
