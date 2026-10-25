# Bridge V3.1 Stage 3C — Fail-closed hole drilling (experimental branch)

## Verified before this branch
- Tekla Structures 2025 SP6 live connection 146 exported successfully.
- 30/30 .NET tests passed, live C++ V3 reader passed.
- 75 attribute records, 65 readable; 1 plate, 1 bolt group with 6 positions,
  1 weld, 1 fitting.
- Axis frame was corrected in Stage 3B; fitting normal (0,-1,0).
- **Bolt axis unresolved**, individual hole diameters unverified, source bolt
  X=0 while actual plate bbox X=4.25..14.25. No OCC solid equivalence proven.

## Stage 3C safety scope
- \`authorizeBridgeAutoDrill\` is a portable, OCC-independent policy.
- Only explicit **measured/observed** axis and measured/observed hole diameters,
  observed 3D positions that lie on/in the plate and match a source target
  plate GUID can authorize automatic cutting.
- An implicit standard M20->22mm hole, bbox-derived axis, re-centered source
  coordinates, wrong plate target or near-edge hole **cannot** authorize cuts.
- \`importBridgeComponent\` still preserves reference plate/bolts metadata and
  preview but will not create inferred-hole cut modifiers.
- \`convertBridgeToNative\` refuses to run the native generator when an
  unverified hole group would otherwise cause fabrication geometry.
- UI and source Tekla file are not modified. The component is **not** yet
  proven parametrically equivalent. This is not an OCC smoke test.

## Windows OCC-off regression

    cd C:\work\model-maker-bridge-v31
    git fetch github
    git switch -c bridge-v31-stage3c-local --track github/tmp/bridge-v3.1-stage3c-safe-drilling-20261025

    cmake --build build-v31-noocc --target model_maker_bridge_auto_drill_policy_test --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Stage 3C build failed" }

    ctest --test-dir build-v31-noocc -R "model_maker_bridge_(auto_drill_policy|v31_package|package_reader|native_mapper|native_bind)_test" --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Bridge regression failed" }

    .\build-v31-noocc\model_maker_bridge_auto_drill_policy_test.exe C:\work\bridge-146-axisfix-test
    if ($LASTEXITCODE -ne 0) { throw "Live 146 drilling policy smoke failed" }

Expected synthetic tests PASS and live \`allowed=false\`, because the source
did not specify independently observed bolt axis/hole diameter. No real OCC
library is needed for this validation.

## Later OCC-on validation
Stage 3C changes OCC-specific importer code too; an OCC-on build and actual
Document import regression must run before the branch is merged anywhere.
Do **not** claim that these source changes are Windows-OCC tested based only
on the OCC-off tests. Never merge this experimental branch into main/PERF
without that check.
