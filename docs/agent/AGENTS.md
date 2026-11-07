# Agent instructions — Model Maker

Rules for AI coding agents (Hermes, Claude Code, Codex, etc.) working in this repository.

## Repository

Model Maker — Tekla-like native steel modelling/detailing.

- Stack: C++20, Qt6, Win32/OpenGL, optional OpenCASCADE (OCCT).
- Linux: headless tests. Windows: GUI/build via MSYS2 UCRT64 (see `docs/WINDOWS_BUILD_TR.md`).
- Develop on the explicitly supplied feature branch; never silently edit `main`.

## Git workflow (worktree clones)

The canonical checkout lives at `C:\Users\Asus\Documents\MEGA\APPS\model-maker`
on branch `feature/component-contract`. Worktree clones (e.g. `C:\work\mm-hermes`)
use a local branch alias:

- Local branch: `hermes/work` (tracks `origin/feature/component-contract`)
- Pull: `git pull --ff-only origin feature/component-contract` (never rebase/merge)
- Push: `git push origin hermes/work:feature/component-contract`
- Half-finished work is committed ONLY to `wip/hermes`, never to `hermes/work`.

## Build and test

- Windows: MSYS2 UCRT64 toolchain; build then run `ctest --output-on-failure`.
- Never claim a build/test pass from code edits alone; report real `FAIL:` diagnostics.

## Repo-local agent skill

- `.hermes/skills/model-maker/SKILL.md` — verified implementation conventions
  (Haunch 40 native component, backward compatibility, regression rules).
- Enable in Hermes with: `hermes skills trust <repo root>`.

## Core conventions

- `GenerationPlan` is the source of truth for native geometry
  (plates, holes, bolts, cuts, welds); never fake geometry with 2D overlays.
- `resultParams()` returns a vector **by value** — bind it to a local variable
  before taking element pointers.
- Keep role IDs, persistent cuts and weld ownership stable across recompute.
