---
name: model-maker-context-control
description: Keep local coding context small and relevant.
version: 1.0.0
author: Selahattin Model-Maker Pack
license: MIT
platforms: [windows]
metadata:
  hermes:
    tags: [context, local-llm, cpp, repository]
    category: software-development
---

# Model-Maker Context Control

## When to Use

Use for every nontrivial Model-Maker task when the main model is a local 30B model
or repository context is large.

## Procedure

1. Restate the task as one measurable outcome.
2. Identify no more than five likely entry files before reading implementation.
3. Search symbols and call sites before opening full files.
4. Read declarations first, then only the implementation ranges required.
5. Maintain a compact working note:
   - goal
   - constraints
   - relevant files
   - discovered invariants
   - open questions
   - validation command
6. Expand scope only when evidence shows another module is involved.
7. After every meaningful tool batch, summarize what changed in understanding instead
   of retaining raw output.
8. Keep generated/build/vendor directories out of context.
9. For broad requests, split work into vertical slices that build and test separately.
10. Never change code merely to simplify the model's reasoning.

## Scope Limits

Default maximum for one change:

- one behavior
- one subsystem
- roughly 3 to 8 source files
- one build target
- one verification path

Exceed these limits only after presenting a staged plan.

## Pitfalls

- Reading dozens of files before locating symbols wastes context.
- Repeating full file contents after small edits increases confusion.
- Mixing rendering, persistence, UI, and geometry refactors in one patch makes local
  models unreliable.
- A long context window is not a substitute for a precise repository map.

## Verification

Before editing, state the selected files and why each is relevant. After editing,
state whether the task remained inside the declared scope.
