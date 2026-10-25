# Native Component Live Preview — P1 + P2 + P3

Branch: \`tmp/component-live-preview-p1p3-20261025\`
Base: \`tmp/haunch40-beam-face-cut-exact-cap-20261025\`
No changes on main; Haunch 40 physical OCC/production materializer unchanged.

## Delivered implementation

### P1: Model Maker's own 3D parameter preview
- New QWidget: \`ComponentLivePreviewWidget\`, C++/Qt only, compiled
  into the existing model-maker GUI and Qt offscreen test.
- On opening Haunch40's existing modeless param dialog, it reads the
  **actual saved ComponentInstanceRecord mainPart/secondaryParts EntityIds**
  and their current Document SteelProfile geometric dimensions, axes and
  section anchor. No fixed HEA400/HEA300 source profile assumptions.
- Re-runs the *production* \`ComponentRegistry::generate("tekla_haunch_40")\`
  in memory and draws real generator PlateSpecs with corner chamfers,
  thickness and depth-sorted 3D shaded faces, real I-shape sections and
  actual generated bolt-grid centre points/shaft directions.
- Important precision boundary: the preview is a lightweight 3D Qt
  software-projection of the NATIVE GENERATION PLAN, not a separate
  OCCT B-Rep tessellation or full weld-mesh display. The actual Document
  is modified only by the existing Apply / Modify buttons.

### P2: Interactive edit controls on 3D
- 7 model-attached \`QDoubleSpinBox\` controls and leader lines:
  \`haunchLength\`, \`haunchDepth\`, \`haunchTipDepth\`,
  \`endPlateThickness\`, \`boltPitchX\`, \`boltPitchY\`,
  \`topPlateThickness\`.
- Each editor binds by NAME to the SAME CompParamSpec and parameter
  form already on the right. Input ranges/units pulled from specs.
- Change either editor: all preview panes regenerate; the right form
  receives the new numeric value. No duplicate independently stored
  geometry parameters. Clicking a field highlights its related
  plate/part and selects the corresponding tab.
- Other advanced settings (material, grade, optional stiffeners, welding)
  remain in the original schema-driven side tabs; changes refresh
  the preview where the generator implements them.

### P3: 3D camera and mini 2D views
- Left mouse drag: orbit. Middle drag: pan. Mouse wheel: zoom.
- Top toolbar: 3D, Ön, Yan, Üst preset camera orientations.
- Bottom strip: actual live-plan 2D Ön/Yan/Üst projections, not stock
  bitmap screenshots. Clicking a mini-view selects its camera.
- Camera changes recompute screen-space leader/editbox positions.
- P3 is the first interactive projection implementation, not GPU-based
  CAD picking/measurement or OCCT renderer embedding.

## Existing behavior preserved
- Previously built Tekla146 schema/photo canvas is NOT modified.
- No changes to Haunch40's verified geometry/cuts/bolts/OCC algorithm,
  undo/redo or MMW8 persistence.
- Applying edits still calls the existing ComponentMaterializer and
  Document recompute. Invalid param combinations show the generator's
  error in preview, without mutating the model.

## Windows PowerShell (OCC + Qt)

    cd C:\work\model-maker-bridge-v31
    Get-Process "model-maker" -ErrorAction SilentlyContinue | Stop-Process -Force
    git fetch github
    git switch -c component-preview-local --track github/tmp/component-live-preview-p1p3-20261025

    cmake -S . -B build-v31-occ -DMM_OCC_LINK_DIRECT=ON
    if ($LASTEXITCODE -ne 0) { throw "Configure failed" }
    cmake --build build-v31-occ --target model-maker model_maker_component_engine_test model_maker_component_dialog_test model_maker_occ_haunch40_native_e2e_test --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }

    ctest --test-dir build-v31-occ -R "^model_maker_(component_engine|component_dialog|occ_haunch40_native_e2e)_test$" --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Regression tests failed" }

    $env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
    & ".\build-v31-occ\model-maker.exe"

### Manual GUI acceptance
1. Open a COPY of your working project with an existing Haunch40
   native instance created on real column/beam.
2. Double-click the connection (or select its owned plate and open
   Component Properties). The modeless dialog must open >=1140x720,
   with real native 3D left, 7 editable mm dimension boxes on leader
   lines, original parameter tabs right, three tiny orthographic panes.
3. Drag/orbit, pan, wheel zoom, and click 3D/Ön/Yan/Üst; ensure editor
   positions track the model and thumbnails show corresponding views.
4. Change Haunch Length e.g. 450 -> 550 via the 3D callout. It must
   appear as 550 in the right form and change the rendered web geometry.
5. Change End Plate Thickness in the right form. The 3D box and
   projected plate should update. Click "Apply" and verify the physical
   OCC component recomputes. Save/reopen project to confirm persistence.
6. Click Get on another Haunch40 instance with different members:
   preview's actual selected section and geometry must update.
7. Open Tekla146 dialog: its existing canvas remains unchanged.

### Validation caveat
Source changes were made through connected GitHub, not built on user's
MSYS2/Qt/OCC toolchain. Do NOT claim this build or GUI test passed
until its actual Windows compile and acceptance output is available.
The Qt offscreen test (only enabled where Qt6 + UNIX are available)
includes the Haunch40 param overlay synchronization case.
