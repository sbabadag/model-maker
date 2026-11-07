# Making a greyed Tekla tab FULLY functional (Bolts tab pattern, 146)

Use when the user says "make tab X fully functional / 1:1": every field on the tab must change
the generated model, or stay visibly grey with a stated reason. Worked example: 146 Bolts tab.

## Order of work

1. Read the Tekla help section for the tab (saved full-text file, see `tekla-dialog-adaptation.md`)
   and list each field with its Tekla meaning. Mark anything the page does not define as
   UNVERIFIED in the final report (e.g. whether slot dimensions are additive to the hole
   diameter; which part "Default" means for "Slots in") - choose the consistent reading, say so.
2. Promote each field: add it to the engine `ComponentType::params` (real enum choices, correct
   TYPE - a Tekla numeric box must be a Length, not an enum), DELETE its `docEnum/docLen` line in
   `component_dialog.cpp` (a duplicate name makes two specs), then read it in the generator.
3. Canvas controls that exist in the generated `component_dialog_146_canvas.cpp` but have NO
   spec are silently dead (the widget skips controls without an editor). Grep the canvas file
   for every control name of the tab and confirm a spec exists.
4. Put the effect on data the geometry layers already consume (`BoltGroup`, `CutModifier`,
   `BoltParams`) - never a parallel path. Defaults must keep the old output identical.
5. Options that need geometry we do not have (sloped/skewed beams, main-part-relative vertical
   dimension): return `plan.ok=false` with an explicit message (fail-closed). Do not map them to a
   neighbour option silently. Controls whose meaning you cannot verify (unlabelled icon
   checkboxes) stay `documentedOnly`; ask the user what they are.
6. Tests: OCC-off generator test per field (`shear_plate_146_bolts_test`), update count
   asserts (`params.size()`, `!documentedOnly` specs, derived-template default checks), then an
   OCC volume test for anything that changes solids.

## Technique notes (each avoided a real bug)

- **`alignParamsToSpecs` resets an unknown enum value to the spec default before the generator
  runs.** A legacy-alias branch inside the generator (`if (x == L"Threaded")`) is dead code;
  add the alias to the choices or drop it.
- **A derived quantity's default should be "auto", not a constant.** `holeDiameter` default 22
  went stale when the bolt size changed; default 0 = catalog (`standardBoltHoleDiameter`),
  tolerance>0 = bolt+tolerance, and clamp hole >= bolt.
- **Per-part hole shape cannot be decided in the generator**: owned plate EntityIds exist only
  at materialization. Carry intent on `BoltGroup` (`holeShapeTarget` 1 plates / 2 user parts /
  3 all) and let the materializer call `resolveHoleShapeParts` after it builds `bolt.parts`
  (BOTH the commit and the recompute site).
- **Slotted/oval hole tool = wire (2 lines + 2 arcs) -> face -> prism, no booleans** (fusing
  cylinder+box is the Windows-crashing pattern). Rounded rectangle with r=min(a,b); stadium area
  = pi r^2 + 2 r L - use it in the volume test. `CutModifier` slot fields persist as TRAILING
  optional fields in the MMW8 `C` line (old files still load).
- **Stack span generalises the grip/centre formulas**: collect `{lo,hi}` of the drilled parts
  along the bolt axis; grip = hi-lo, shift = (hi+lo)/2. Cut length and near/far tab variants then
  fall out as filtering the span list; excluding a part the materializer adds automatically
  (the owned plate) is not possible -> error instead.
- **Stagger patterns live in `boltGroupBoltOrigins`** (display and holes both use it): rows are
  counted from the TOP (j=0 is the bottom row, so rowFromTop = countY - j); widen the group span
  by the stagger offset BEFORE centring, and in the plate-width pre-pass.
- Bolt-length increase belongs to the display solid only: extend the shank on the nut side and
  shift nut/washer by dir*inc.
- Hole-modifier counts = bolt positions x parts; assert `positions*parts` in tests, not 1 per part.

## Reading small Tekla icons when vision fails

`vision_analyze` and child vision models failed on tiny help icons; children fell back to pixel
analysis. A reliable fallback is ASCII-rendering the crop (PIL, threshold into `#`/`+`/`.`) to read
checkbox ticked/empty state and to count bolt positions in staggering icons. Icon IDENTITY (which
checkbox is washer vs nut) cannot be inferred from pixels: ask the user to look at the real Tekla
dialog. `delegate_task` accepts at most 8 images per task.
