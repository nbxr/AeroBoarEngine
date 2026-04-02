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
- You are an expert coding agent. When using tools like read_file or edit, always use valid absolute paths inside the workspace. Never pass undefined or empty filepath.
- use pwd to find root directory
- use only the tool cals listed below
- In Plan mode, only these read-only tools are available:
    - Read file (read_file)
    - Read currently open file (read_currently_open_file)
    - List directory (ls)    
    - grep_search : `query` argument is required and must not be empty or whitespace-only
    - Fetch URL content (fetch_url_content)
    - Search web (search_web)
    - View diff (view_diff)
    - View repo map (view_repo_map)
    - View subdirectory (view_subdirectory)
    - Codebase tool (codebase_tool)
- Tools Are Available in Agent Mode (All Tools). In Agent mode, all tools are available including the read-only tools above plus:
    - Create new file (create_new_file): Create a new file within the project
    - edit_existing_file : `filepath` and `changes` arguments are required to edit an existing file
    - Run terminal command (run_terminal_command): Run commands from the workspace root
    - Create Rule Block (create_rule_block): Create a new rule block in .continue/rules
- These tools are not supported: glob_search, edit_file