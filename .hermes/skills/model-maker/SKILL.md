---
name: model-maker
description: Model Maker (C++20/Qt6/OpenCASCADE) steel detailing implementation, regression, and Haunch 40 native component conventions.
---

# Model Maker — verified implementation conventions

## Context
- Repository: `sbabadag/model-maker`, Tekla-like native steel modelling/detailing.
- Stack: C++20, Qt6, Win32/OpenGL, optional OpenCASCADE (OCCT). Linux headless tests and Windows GUI/build are distinct.
- Develop on the explicitly supplied feature branch, **never silently edit `main`**. For Oct 2026 Haunch experiments: `tmp/component-preview-context-p4p6-20261025`.
- The generated `GenerationPlan` is the source of truth for native plates, drill holes, bolts, cuts, welds and 3D mockup; do not fake geometry with 2D overlays.
- Never claim Windows build/tests pass based only on GitHub edits. Request precise `FAIL:` diagnostics after a failed CTest and repair the actual failing assertion/bug.

## Critical Haunch 40 design — two independent bolt groups
- Native roles: `haunch_bolts_upper` and `haunch_bolts_lower`. These are **independent physical groups**, not two views of a single shared grid.
- Upper properties: `upperBoltCountX/Y`, `upperBoltPitchX/Y`, `upperBoltDiameter`, `upperHoleDiameter`, `upperBoltEdgeClear`.
- Lower properties: analogous `lowerBolt...` and `lowerHoleDiameter`.
- Only ONE vertical clearance per group: upper from **top end-plate edge** to the OUTSIDE EDGE of uppermost drill hole; lower from **bottom end-plate edge** to the OUTSIDE EDGE of bottommost drill hole. These are NET clearances, not centre offsets.
- For generated hole centres, `eT_center = upperBoltEdgeClear + upperHoleDiameter/2` and `eB_center = lowerBoltEdgeClear + lowerHoleDiameter/2`.
- Changing upper clearance translates **ONLY** the upper group vertically; changing lower clearance translates **ONLY** the lower group. The supporting column, end plate (dimensions/centre), opposite bolt group, row counts and pitches remain unchanged by these edge edits. Do not resize plates to satisfy edge offsets: clamp/reject geometrically impossible inputs.
- `endPlateWidth/Height` are separate explicit plate controls; `0` retains structural auto-size. Explicitly reducing plate dimensions is allowed until real physical hole/plate clearance and inter-group collision limits.
- Two group grids can have different counts, hole/bolt diameters and pitches. Compute actual total bolts as `upper.nx * upper.ny + lower.nx * lower.ny`, never twice one group's count.
- Shared legacy side margins `boltEdgeLeft/Right` and historic `boltEdge` are separate from the vertical group clearances. Horizontal alignment is not a reason to couple upper/lower vertical dimensions.
- Use selected-role-only 3D editors: 7 upper fields when upper is selected, 7 lower fields when lower is selected. Right form has separate upper/lower tabs. Data editing propagates to generator and all orthographic views.
- Measure dimension labels from actual `boltGroupBoltOrigins` projected into the real end plate local frame; avoid hardcoded labels.

## Backward compatibility
- Existing records (including MMW8-era components) can lack group-scoped parameters. In `migrateHaunch40Edges`, inherit shared historical count/pitch/diameter/hole values into EACH group only when that group's key is absent; never overwrite saved independent settings.
- Previous `boltEdgeTop/Bottom` values measured to hole CENTRES. Migrate to new net-clearance inputs by subtracting that group's hole radius. Previous `boltEdge` structural margin is retained for the end plate auto-dimension policy.
- Legacy automatic plate height and centre must not depend on new upper/lower group offsets; preserve old default positioning as a check.
- `resultParams()` returns a vector **by value**: do not obtain `CompParam*` from a temporary `resultParams()` vector. Bind the returned vector to a local variable first.

## Safety, regression and diagnostics
- Test at least: legacy 2x2+2x2; independent upper 3x2 + lower 2x2; independent diameters; upper-only and lower-only translations; shorter explicit plates; no collisions/hole overhang; exact legacy migration; 3D selection and right-editor synchronization; no Document writes until Apply/Modify.
- Qt offscreen `propagateSizeHints` / `raise` warnings are not themselves failing tests; capture the actual `FAIL:` message.
- Windows project example: `C:\\work\\model-maker-bridge-v31`, MSYS2 UCRT64 toolchain, `build-v31-occ`. Build GUI/engine/widget tests and run `ctest --output-on-failure` before declaring verified.
- Keep role IDs, persistent cuts and weld ownership stable across recompute.

## Important limitation
This tracked file documents skill behaviour in the GitHub working branch. Hermes on AWS will only use it once its skill loader is configured to read this path or this file is copied/installed into the Hermes skills directory. Never claim remote skill installation without checking there.
