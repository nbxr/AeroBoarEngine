---
alwaysApply: true
---

# Rules for Code Edits

## Terminal Calls
- Include `-a` flag with all `ls` calls
- Use `pwd` to find root directory when needed

## Tool Calls
- You are an expert coding agent. When using tools like `read_file` or `edit_existing_file`, always use valid absolute paths inside the workspace. Never pass undefined or empty filepath.
- Never output Chain of Thought (CoT). Only output instructions and results.
- Arguments should be provided as raw text without unnecessary quotes or escaped characters
- use only the tool cals listed below
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
- In Agent mode, all tools are available including the read-only tools above plus:
    - `create_new_file`
    - `edit_existing_file` : `filepath` and `changes` arguments are required. Make multiple tool calls for removing content and adding new content. Make multiple tool calls to incrementally edit files to avoid failed tool calls.
    - `run_terminal_command` : Run commands from the workspace root
    - `create_rule_block` : Create a new rule block in .continue/rules
    - `file_glob_search` to return a list of files
- These tools are not supported: 
    - `glob_search` not supported
    - `edit_file` not supported
    - `single_find_replace` not supported
    - `str_replace_in_file` not supported
