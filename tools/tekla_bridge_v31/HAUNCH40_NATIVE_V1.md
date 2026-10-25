# Model Maker native Haunch (40) — V1

Branch: tmp/haunch40-native-v1-20261025
Base: tmp/bridge-v3.1-stage4b-role-based-binding-20261025
Production main was NOT changed.

## Supported RIGHT NOW
- New registered editable component: \`tekla_haunch_40\`, visible under
  **Çelik → Component Kataloğu → Haunch (40)**.
- Uses ACTUAL Model Maker main column + secondary beam profiles, orientation
  and contact plane. No exact Tekla HEA profile-name matching.
- Creates real BRep plate parts: 20-mm bolted end plate (automatic from beam
  width/height), a near-triangular tapered 10-mm haunch web, 10-mm inclined
  compression flange, and optional 10-mm top plate. Beam stiffener optional.
- Two 2x2 M20 bolt groups (8 bolts, 22-mm round holes) tied to the column.
- Fillet weld relationships end/web/compression flange/top plate to beam.
- Native Document component identity + stable semantic role keys, undo/redo,
  saved typed params and recompute supported by existing framework.
- 6-tab editable parameter dialog: geometry, haunch, end plate, stiffeners,
  bolts, welds. Rejects geometrically impossible taper/bolt edge fits.
- Component is **PARTIALLY_IMPLEMENTED**, not a full 1:1 Tekla 40 clone.

Starting defaults draw from the previously observed connection-40 probe:
- Sample had 7 contour plates + 3 Beam-style plate pieces,
  2 bolt arrays with 4 M20 bolts each, many welds.
- PL20*200 end plate, PL10*230 beam-style plate,
  welded haunch length around 450mm, default M20 groups 120x80 pitch.
- These are reference defaults, NOT complete reconstruction of every
  plate, fitting, 29 weld operations, Tekla special cut and unknown inputs.

Unsupported in this V1: profile-based haunch option, open beam flange cutting,
certain cap/compression/extra plates, Tekla-specific flange notches, exact
weld topology, 40-specific attribute import and direct 1:1 geometry parity.

## Windows PowerShell (OCC-enabled, close running EXE first)

    cd C:\work\model-maker-bridge-v31
    Get-Process "model-maker" -ErrorAction SilentlyContinue | Stop-Process -Force

    git fetch github
    git switch -c haunch40-native-local --track github/tmp/haunch40-native-v1-20261025

    cmake -S . -B build-v31-occ -DMM_OCC_LINK_DIRECT=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure error" }

    cmake --build build-v31-occ --target model_maker_component_engine_test model_maker_component_dialog_test model_maker_component_catalog_test model_maker_occ_haunch40_native_e2e_test model-maker --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Native 40 build error" }

    ctest --test-dir build-v31-occ -R "^model_maker_(component_engine|component_dialog|component_catalog|occ_haunch40_native_e2e)_test$" --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Native 40 tests failed" }

    $env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
    & ".\build-v31-occ\model-maker.exe"

**Note**: CTest names are:
- \`model_maker_component_engine_test\`
- \`model_maker_component_dialog_test\`
- \`model_maker_component_catalog_test\`
- \`model_maker_occ_haunch40_native_e2e_test\`

## Usage
Model Maker → Çelik → Component Kataloğu → Haunch (40).
Click **column FIRST**, **beam SECOND**, right-click to finish.
Choose generated owned plate/bolt -> parametric component properties → edit
L/D/t/bolt parameters -> recompute from selected real members.
The test includes reversed input, but the production catalog stores the
user's explicit first click as main, so prefer the documented order.

Linux/Windows tests are not reported as passing until actually run.
