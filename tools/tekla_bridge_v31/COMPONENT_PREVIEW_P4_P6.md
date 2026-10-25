# Native Component 3D Editor P4–P6: selection, context, dependent geometry

Branch: \`tmp/component-preview-context-p4p6-20261025\`
Base: \`tmp/component-live-preview-p1p3-20261025\`
No production main modifications. No changes to the native Haunch 40 steel
generator's fabrication geometry, OpenCASCADE cut/drill rules, or Document data.

## P4 — contextual selected element callouts

- Select a plate by clicking its shaded 3D polygon, or a bolt group by
  clicking its visible bolt head/shank in the main preview.
- Clear the selection by clicking empty 3D background.
- Orbit still uses left-button DRAG; click-vs-drag threshold = 5 screen px.
- Only the selected role's measurement spinboxes are visible.
- Main/secondary steel members remain selectable/context, without editable
  3D overlays. The sidebar retains the full supported param set.
- Per-role editors:
  - \`haunch_web\`: haunchLength, haunchDepth, haunchTipDepth,
    haunchWebThickness
  - \`haunch_lower_flange\`: haunchFlangeThickness, haunchFlangeWidth
  - \`haunch_end_plate\`: endPlateThickness, endPlateWidth, endPlateHeight
  - \`haunch_bolts_upper\` and \`haunch_bolts_lower\`: boltPitchX,
    boltPitchY, boltDiameter, boltEdge, anchored to the CLICKED group
  - \`haunch_top_plate\`: topPlateThickness
  - \`haunch_beam_stiffener\` if present: stiffenerThickness
- All editors share existing CompParamSpec ranges/unit constraints.
- Right sidebar and 3D editor still send values through the SAME
  \`ComponentDialogWidget::resultParams()\` path. No duplicated truth.
- If an optional child is disabled while selected, the selection clears.

## P5 — focus without losing physical parent context

- Selected child is emphasized; unselected children are translucent,
  parent column/beam stay in the same 3D scene at 95-alpha, NOT hidden.
- Camera moves toward selection by a LIMITED screen-space pan (max
  15% width / 12% height), so distant cap selection does not throw the
  beam or main column off-screen.
- The parent column's visible span is measured from ALL native plates
  (including the high cap plate), not cropped around only the end plate.
  This avoids a fake, floating cap disconnected from the displayed column.
- Main beam in preview is bounded by the production component's real
  generated PlaneCut at the column/end plate. The preview no longer
  shows a beam visually penetrating the end plate.
- Left drag orbit, middle drag pan, wheel zoom, and iso/orthographic mini
  previews retain prior functionality.

## P6 — dependent recompute

- Both form and 3D overlay edits re-run the ENTIRE
  \`ComponentRegistry::generate("tekla_haunch_40")\` plan, never mutate only
  an isolated child mesh. All four preview panes see the same parameters.
- Generator geometry remains linked: resized end plate -> cut plane and
  bolt grip, changed haunch web -> inclined lower flange endpoints,
  cap thickness -> lower face stays at physical column top and footprint
  is inherited from the selected steel section.
- A new C++ generator regression changes SEVEN params together:
  haunchLength, haunchDepth, endPlateThickness, gap,
  topPlateThickness, boltPitchX, boltPitchY, then verifies
  BRep plan consistency, inclined flange contact, cap position, cut,
  all eight real bolt positions and grip source.
- Qt offscreen test includes contextual filtering, lower-group selection,
  clearing overlays, and one live web-length->form->generator roundtrip.
- Actual Document is updated ONLY by the existing Apply/Modify control;
  invalid geometry stays out of Document with the generator's error.
- The full OCC native Haunch 40 regression must continue to pass.

## Windows: update and verify

    cd C:\work\model-maker-bridge-v31
    Get-Process "model-maker" -ErrorAction SilentlyContinue | Stop-Process -Force
    git fetch github
    git switch -c preview-context-local --track github/tmp/component-preview-context-p4p6-20261025

    cmake -S . -B build-v31-occ -DMM_OCC_LINK_DIRECT=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure error" }

    cmake --build build-v31-occ --target model-maker model_maker_component_engine_test model_maker_component_dialog_test model_maker_component_catalog_test model_maker_occ_haunch40_native_e2e_test --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Build error" }

    ctest --test-dir build-v31-occ -R "^model_maker_(component_engine|component_dialog|component_catalog|occ_haunch40_native_e2e)_test$" --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Regressions failed" }

    $env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
    & ".\build-v31-occ\model-maker.exe"

### Manual acceptance (Haunch40 on a copy of the project)

1. Open Haunch40 component's parameters; confirm NO floating 3D edit
   boxes before clicking a part.
2. Click purple web -> four web-only boxes. Change L 450 -> 600.
   The web extends; inclined bottom flange stays in contact and the
   real column + beam remain in view.
3. Click orange inclined lower flange -> two flange-only boxes.
4. Click end plate -> three end-plate boxes. Change t 20 -> 30.
   The preview's beam cut face and physical 8-bolt grip must update.
5. Click LOWER bolt group -> four bolt fields near LOWER, not upper,
   group. Change pitchX/boltEdge and verify actual 2x2 pattern.
6. Click column cap -> only cap thickness; cap remains attached at
   top of the real column. Increase cap thickness then Apply.
7. Empty click -> all floating boxes disappear; supporting members
   and mini orthographic views remain visible.
8. Apply -> real OCC Document recompute. Undo/Redo as usual.

### Verification status

The connected GitHub edits are committed, but a Windows OCC/Qt build and
real 3D GUI acceptance were not run by the assistant. CTest results are
targets until the user executes them, never to be reported as passed.
The offscreen Qt widget test is currently built by CMake only with
Qt6 on UNIX; it is NOT one of the Windows CTest targets above.
