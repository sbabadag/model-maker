---
name: model-maker-repo-map
description: Map the Model-Maker repository before editing.
version: 1.0.0
author: Selahattin Model-Maker Pack
license: MIT
platforms: [windows]
metadata:
  hermes:
    tags: [cpp, opengl, cad, repository, windows]
    category: software-development
    requires_toolsets: [terminal]
    config:
      - key: model_maker.project_root
        description: Model-Maker repository root.
        default: C:\Users\Asus\Documents\MEGA\APPS\model-maker
        prompt: Model-Maker repository root
---

# Model-Maker Repository Map

## When to Use

Use before the first code change in a session, after a major refactor, or when the
requested feature crosses several modules.

## Procedure

1. Resolve the configured project root. Confirm it exists with the real `terminal`
   tool. Never print pseudo tool markup such as `<function=terminal>`.
2. Treat the host as Windows and use PowerShell. Do not use Linux commands such as
   `find`, `grep`, `head`, `sed`, `awk`, or slash-based Windows paths.
3. Run a read-only first pass:

   ```powershell
   $root = "C:\Users\Asus\Documents\MEGA\APPS\model-maker"
   Get-ChildItem $root -Force
   Get-ChildItem $root -Recurse -File `
     -Include *.sln,*.vcxproj,CMakeLists.txt,CMakePresets.json,*.cpp,*.c,*.h,*.hpp `
     | Select-Object -First 250 -ExpandProperty FullName
   ```

4. Use `search_files` or PowerShell `Select-String` to locate:
   - application entry points
   - renderer and OpenGL resource ownership
   - scene/object/entity storage
   - camera and viewport code
   - selection and picking
   - spatial indexes
   - undo/redo and commands
   - serialization
   - tests and benchmarks
5. Read only the smallest useful set of files. Do not dump the whole repository into
   context.
6. Produce a concise map containing:
   - build system and primary targets
   - major modules and responsibilities
   - runtime data flow
   - likely change points for the current task
   - known uncertainties
7. Do not edit files unless the user explicitly asks for implementation.

## Pitfalls

- A filename is not proof of responsibility; inspect declarations and call sites.
- Parentheses in `Get-ChildItem -Include` and pipeline precedence can produce wrong
  results. Prefer one PowerShell pipeline with `-File -Include`.
- Do not claim a command ran unless a tool result was returned.
- Do not recursively read generated directories such as `.git`, `build`, `out`,
  `.vs`, `x64`, `Debug`, `Release`, `node_modules`, or third-party dependency trees.

## Verification

Report the exact root inspected and name at least one concrete file supporting each
major architectural claim.
