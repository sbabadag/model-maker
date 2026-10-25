# Haunch (40) bolt placement hotfix — real plate coordinate frame

Issue observed on the user's Windows OCC model:
- Haunch plates were built, but M20 shanks protruded well beyond the
  column/end plate and appeared on the opposite support side.
- The first 40 generator never set BoltGroup.gripLength / holeDepth /
  bolting direction, and the materializer automatically appended EVERY
  component-owned plate to each group's drilled parts.
- Document::boltGripLength computed the distance through the entire
  column BRep bbox plus haunch plates (hundreds of mm), not merely the
  end-plate + flange stack. Consequently the bolt shank protruded.
- Both bolt arrays do already use end.center and W/H mathematically,
  but the axial offset was not at the actual plate + column-flange
  stack midpoint. This hotfix corrects the stack depth and preserves
  the actual end-plate U/V frame rather than using world coordinates.

## Changes

- Generator sets group position from \`end.center + end.u/end.v\` and
  recenters it axially through the real end plate + column flange.
- End-plate face contact is source \`p.facePoint\`, with normal pointing
  into the beam. Bolt head is on the accessible beam (+normal) side,
  nut on the interior column (-normal) side (\`direction = 2\`).
- Real shank grip = endPlateThickness + gap + selected column's
  actual \`secFlangeThickness\`, e.g. 20 + 0 + 19 = 39mm for HEB300.
- Finite actual holeDepth = grip + 2mm (1mm clearance each end).
- The materializer can target a single owned component plate by stable
  role name \`BoltGroup.targetOwnedPlateRole\`. Haunch40 uses
  \`haunch_end_plate\` + selected real column ONLY.
- Generic non-Haunch components that leave this role empty retain
  prior behavior of including all owned plates.
- Fail closed when a target role is missing or target column flange
  thickness is unknown, before committing any partial Document changes.
- Applied to both commit and recompute paths. Saved \`bolt.parts\`
  contains resolved target EntityIds; regenerated plans recover the
  semantic \`targetOwnedPlateRole\` on recompute.
- No changes to Tekla 146 geometry or global bolt orientation.

## Windows acceptance

    cd C:\work\model-maker-bridge-v31
    Get-Process "model-maker" -ErrorAction SilentlyContinue | Stop-Process -Force
    git fetch github
    git switch -c haunch40-bolt-hotfix-local --track github/tmp/haunch40-bolt-placement-hotfix-20261025
    cmake -S . -B build-v31-occ -DMM_OCC_LINK_DIRECT=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake error' }
    cmake --build build-v31-occ --target model_maker_component_engine_test model_maker_component_catalog_test model_maker_occ_haunch40_native_e2e_test model-maker --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Build error' }
    ctest --test-dir build-v31-occ -R '^model_maker_(component_engine|component_catalog|occ_haunch40_native_e2e)_test$' --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Test error' }
    $env:PATH="C:\msys64\ucrt64\bin;$env:PATH"
    & ".\build-v31-occ\model-maker.exe"

IMPORTANT: delete the PREVIOUS wrongly generated native Haunch40 instance
or undo it, then choose "Haunch (40)" and select the target column/beam
again. Restarting the EXE alone does not retroactively repair saved bolt
positions or old component-owned Boolean cuts without an explicit recompute.
The first smoke should create a new connection on a copy of the project.

Tests are NOT marked passed until actually executed on Windows.
