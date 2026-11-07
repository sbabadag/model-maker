# Agent instructions — Model Maker

Rules for AI coding agents (Hermes, Claude Code, Codex, etc.) working in this repository.

## Repository

Model Maker — Tekla-like native steel modelling/detailing.

- Stack: C++20, Qt6, Win32/OpenGL, optional OpenCASCADE (OCCT).
- Linux: headless tests (CI: GitHub Actions). Windows: GUI/build via MSYS2 UCRT64,
  Git Bash shell (see `docs/WINDOWS_BUILD_TR.md`, `docs/agent/STATE.md`).
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

- Windows (Git Bash): `export PATH=/c/msys64/ucrt64/bin:$PATH` and
  `export QT_PLUGIN_PATH=/c/msys64/ucrt64/share/qt6/plugins` before every build
  command; build dir `build-hermes` (Release, Ninja, UCRT64 g++ — full config
  in `docs/agent/STATE.md`); then `ctest --output-on-failure`.
- Linux: GitHub Actions CI (Qt6 + OCCT, offscreen, full ctest).
- Never claim a build/test pass from code edits alone; report real `FAIL:` diagnostics.

## Session protocol

- Session start: `git pull --ff-only origin feature/component-contract`, read
  `docs/agent/STATE.md`, copy `docs/agent/skills/*` into the local Hermes skills
  folder.
- Session end ("devir" or work finished): update `docs/agent/STATE.md`, commit,
  push.
- UI roadmap: `docs/agent/ROADMAP-UI.md`; the user keeps long message texts in
  files (e.g. `C:\work\HERMES-*.md`).

## Sound alert (user notification)

- When a task finishes, a DURAK point is reached, or you wait for the user's
  answer/approval, beep as the LAST step of the turn (Git Bash):

      powershell.exe -NoProfile -Command "[console]::beep(880,250);[console]::beep(1320,350)"

- When stopping after an error/failure, use the distinct error beep:

      powershell.exe -NoProfile -Command "[console]::beep(400,600)"

- No beeps in the middle of long-running work; only at finish / wait / error.

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

## Planlayıcı görevleri

Oturum başında `docs/agent/STATE.md` ile birlikte `docs/agent/INBOX.md`
okunur; görev sırası ve durumları oradadır.
