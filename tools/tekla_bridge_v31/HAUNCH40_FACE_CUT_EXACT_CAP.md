# Haunch 40 — Beam flush cut and exact column-profile cap

Base: tmp/haunch40-slope-column-cap-fix-20261025
This is a separate experimental branch, NOT main.

## Fix 1 — beam trims at the real column face / bolted end plate

Previous Haunch40 generated its end plate/haunch but NEVER generated a
CutModifier on the physical selected secondary beam. If the user beam
extended through the column, the connection left it untouched.

Now generateHaunch40 creates exactly one component-owned hidden PlaneCut:
- target = the selected actual secondary beam entity id (role-resolved)
- planeOrigin = source contact point on column face
               + beam outward unit direction * (gap + endPlateThickness)
- planeNormal = beam outward direction
- keepPositive = true (preserves the part of beam away from the column)
- roleKey = "haunch_beam_end_cut", stable on recompute.

The physical end plate spans [column-face + gap, column-face + gap +
endPlateThickness]. This cut causes the trimmed beam end to meet the
OUTER/BEAM-SIDE face of the end plate, with no beam penetrating end plate
or column. Setting gap=0 means the end plate's column-side face sits
on the column contact plane. Cutting at the bare column face without
the plate thickness would make the welded beam overlap the end plate.

The production ComponentMaterializer already resolves/commits and applies
all component-owned PlaneCuts to actual OCC B-Rep solids. This path is
also executed on component recompute (and is removed on undo/delete).
The code does not shorten the member parametric axis; it physically
trims the B-Rep via a source-owned persistent modifier.

## Fix 2 — column top cap equals real section b × h (no margin)

Previously cap was oriented by the BEAM plan direction, measured from
a rotated bbox of the selected column, expanded by 20 mm each dimension,
and optionally overridden by the legacy topPlateLength (230 mm).
The result was systematically larger than the actual HEB/HEA column.

Now cap:
- uses the selected COLUMN's own section-frame width vector in plan,
  with second vector = world Z cross first vector;
- width = p.col.secWidth EXACT
- height = p.col.secHeight EXACT
- no extra 20mm, no projected rotated beam-aligned bbox
- lower face = top of physical selected column (retained previous fix).
- cap stays horizontal; actual thickness topPlateThickness stays editable.
- legacy topPlateLength parameter is retained for old MMW8 records but
  does not override the automatically determined cap plan size and has
  been hidden from the "Kolon Cap" input tab.

Regression tests: C++ component engine checks exactly one cut, correct
selected beam target and cut side; moving endPlateThickness/gap moves
plane 1:1; cap 300×400 on example column regardless of IPE secondary.
OCC acceptance tests require the selected beam B-Rep to be physically
trimmed, 16 bolt holes + one beam end plane modifier, and HEB300 cap
exact 300×300 on the real top face.

## Windows

    cd C:\work\model-maker-bridge-v31
    Get-Process "model-maker" -ErrorAction SilentlyContinue | Stop-Process -Force
    git fetch github
    git switch -c haunch40-facecut-local --track github/tmp/haunch40-beam-face-cut-exact-cap-20261025

    cmake -S . -B build-v31-occ -DMM_OCC_LINK_DIRECT=ON
    if ($LASTEXITCODE -ne 0) { throw "Configure failed" }
    cmake --build build-v31-occ --target model_maker_component_engine_test model_maker_component_dialog_test model_maker_component_catalog_test model_maker_occ_haunch40_native_e2e_test model-maker --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
    ctest --test-dir build-v31-occ -R "^model_maker_(component_engine|component_dialog|component_catalog|occ_haunch40_native_e2e)_test$" --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
    $env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
    & ".\build-v31-occ\model-maker.exe"

Run on a COPY of the working document; delete old, incorrectly built
Haunch40 and create a NEW one. Do not call Windows or OCC acceptance
tests "passed" before the user has run them.
