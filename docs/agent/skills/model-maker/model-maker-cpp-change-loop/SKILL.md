---
name: model-maker-cpp-change-loop
description: Make safe, reviewable Model-Maker C++ changes.
version: 1.0.0
author: Selahattin Model-Maker Pack
license: MIT
platforms: [windows]
metadata:
  hermes:
    tags: [cpp, refactor, patch, testing, cad]
    category: software-development
    requires_toolsets: [terminal]
---

# Model-Maker C++ Change Loop

## When to Use

Use when implementing or repairing a Model-Maker C++ feature.

## Procedure

1. Confirm the requested behavior and acceptance test.
2. Inspect repository status:

   ```powershell
   Set-Location "C:\Users\Asus\Documents\MEGA\APPS\model-maker"
   git status --short
   ```

   If the directory is not a Git repository, state that and continue carefully.

3. Locate declarations, definitions, call sites, ownership, and tests.
4. Give a plan of at most five steps.
5. Prefer the smallest coherent patch:
   - preserve public interfaces unless change is required
   - preserve ownership and lifetime rules
   - avoid unrelated renaming or formatting
   - use existing project conventions
6. Apply edits using `patch` when possible. Re-read edited regions.
7. Build the smallest affected target.
8. Run focused tests or a reproducible smoke check.
9. If verification fails, diagnose from the first relevant error rather than making
   speculative broad edits.
10. Report:
    - files changed
    - behavior changed
    - build/test result
    - remaining risk
    - suggested next isolated task

## C++ Rules

- Prefer RAII for OpenGL and native resources.
- Make ownership explicit.
- Avoid hidden per-frame allocations.
- Avoid copying large geometry containers.
- Do not introduce global mutable state without a documented reason.
- Maintain const-correctness where consistent with existing code.
- Check thread affinity for OpenGL calls.
- Keep render-thread and model-thread responsibilities clear.

## Safety

Do not commit, push, reset, delete, mass-format, or upgrade dependencies without
explicit approval.

## Verification

A change is not complete until the affected target builds or the exact blocking
error is reported with evidence.
