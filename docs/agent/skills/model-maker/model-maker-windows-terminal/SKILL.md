---
name: model-maker-windows-terminal
description: Run Model-Maker commands correctly on Windows.
version: 1.0.1
author: Selahattin Model-Maker Pack
license: MIT
platforms: [windows]
metadata:
  hermes:
    tags: [windows, powershell, terminal, cpp]
    category: software-development
    requires_toolsets: [terminal]
---

# Model-Maker Windows Terminal Discipline

## When to Use

Use whenever inspecting, building, testing, searching, or modifying Model-Maker on
the user's Windows computer.

## Hard Rules

1. Use the real `terminal` tool. Never emit XML-like or text-only tool calls.
2. The shell is PowerShell unless tool output proves otherwise.
3. Quote paths containing spaces and use Windows paths.
4. Never fabricate terminal output.
   Native MSYS2 tools need NATIVE temp paths: if `TMP` is an MSYS path (`/tmp`),
   g++/linker fail instantly with `Cannot create temporary file in C:\Windows\:
   Permission denied`. Export `TMP`/`TEMP`/`TMPDIR=C:\work\mm-tmp` in every
   build/test command (details: `docs/agent/known-failures.md` §4).
5. If a tool call fails, show the actual error and repair the command.
6. Do not switch to WSL, Git Bash, Docker, or a remote shell without explicit
   permission.

## PowerShell Equivalents

| Intent | Use |
|---|---|
| Find files | `Get-ChildItem -Recurse -File` |
| Search text | `Select-String` or `search_files` |
| First N items | `Select-Object -First N` |
| Read file | `Get-Content -Raw` or `read_file` |
| Current path | `Get-Location` |
| Test path | `Test-Path` |
| Process list | `Get-Process` |
| Environment variable | `$env:NAME` |

Example file scan:

```powershell
Get-ChildItem "C:\Users\Asus\Documents\MEGA\APPS\model-maker" `
  -Recurse -File -Include *.cpp,*.h,*.hpp |
Select-Object -First 40 -ExpandProperty FullName
```

Example text search:

```powershell
Get-ChildItem $root -Recurse -File -Include *.cpp,*.h,*.hpp |
Select-String -Pattern "glDraw|glBuffer|Picking|Selection" |
Select-Object -First 100
```

## Safety

Before destructive or broad commands, stop and ask for approval. This includes:

- recursive deletion
- `git reset --hard`
- `git clean`
- overwriting project or solution files
- mass formatting
- dependency upgrades
- rewriting generated project files

## Verification

Every reported result must be traceable to a real tool response. For edits, list the
exact files changed.
