---
name: model-maker-weld-double-sided
description: "Use when handling double-sided weld generation in Model Maker CAD repository."
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux, windows]
metadata:
  hermes:
    tags: [weld, double-sided, CAD]
    related_skills: [model-maker]
---

# model-maker-weld-double-sided

## Procedure
1. When constructing weld geometry, inspect the `Weld` object's `doubleSided` boolean.
2. In `buildWeldSolids`, iterate over each `WeldJunction`:
   - If `w.doubleSided` is true, generate fillet solids for **both** possible sides (`side == 0` and `side == 1`) and add each solid to the result list.
   - If `w.doubleSided` is false, generate a solid for only the first side (`side == 0`) and skip the opposite side.
3. Accumulate the total seam length for all generated solids.
4. If no solids are produced for a junction, record a meaningful `unsupportedReason` describing why placement failed.

## Pitfall
- **Missing double‑sided check:** If code generates solids for both sides regardless of `doubleSided`, extra fillet solids appear when the user requested a single‑sided weld, leading to unexpected geometry and test failures. Conversely, restricting generation to a single side when `doubleSided` is true results in missing fillet solids on the opposite face. Always branch on `w.doubleSided` before the side loop.

## Reference
See `references/weld-double-sided-rule.md` for a concise decision table and example code snippets.
