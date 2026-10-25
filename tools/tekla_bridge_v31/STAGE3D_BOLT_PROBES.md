# Stage 3D — Tekla 2025 SP6 bolt coordinate and hole settings probes

Basis: live Tekla connection 146, tested on Stage 3C:
- 30/30 C# Stage 3B tests and 5/5 C++ Bridge tests passed.
- Real OCC smoke and 3/3 OCC fixtures passed.
- Plate 10 x 170 x 200 mm, 6 M20 bolt positions, 65/75 GetAttribute readings.
- Bolt positions returned source X=0 while plate bbox X=4.25..14.25.
- The source axis and physical round-hole diameter are **not yet verified**.
- Fail-closed Stage 3C OCC importer generated a 340000 mm^3 plate and 0 holes.

Tekla 2025 SDK sources:
- BoltGroup.BoltPositions: coordinates are in the XY plane of the bolt-group
  coordinate system and relative to the transformation plane where selected:
  https://developer.tekla.com/doc/tekla-structures/2025/bolt-positions-property-52078
- BoltGroup.GetCoordinateSystem inherited API:
  https://developer.tekla.com/doc/tekla-structures/2025/bolt-group-methods-52113
- CoordinateSystem Z = X cross Y:
  https://developer.tekla.com/doc/tekla-structures/2025/coordinate-system-class-51473
- Hole type/slotted/tolerance properties:
  https://developer.tekla.com/doc/tekla-structures/2025/bolt-group-properties-52074

## What this change does
- Adds optional \`frameProbe\` to each BoltGroup in objects.json:
  orthonormal coordinateSystem + normalCandidate, source label,
  positionReference note and **verifiedForDrilling=false**.
- Adds optional \`holeSettingsProbe\`: Tekla plain/special hole types,
  slotted-hole dimensions and five per-part hole flags, with
  **verifiedForDrilling=false**.
- Leaves authoritative drilling fields \`axis=""\`, \`axisSource=""\`,
  \`direction=null\`, \`bolts[].holeDiameter=0\` unchanged.
- Preserves source BoltPositions literally. No projection/recenter and no
  assumed 22mm physical hole. C++ V3 reader ignores optional new probe
  fields, while all existing checksum invariants remain enforced.

## Windows test (separate branch, no main changes)

    cd C:\work\model-maker-bridge-v31
    git fetch github
    git switch -c bridge-v31-stage3d-local --track github/tmp/bridge-v3.1-stage3d-bolt-probe-20261025
    $root = 'tools\model-maker-bridge\tekla-extractor'

    dotnet test "$root\tests\ModelMaker.TeklaBridge.Core.Tests\ModelMaker.TeklaBridge.Core.Tests.csproj" -c Release --nologo
    if ($LASTEXITCODE -ne 0) { throw "C# tests failed" }

    dotnet build "$root\src\ModelMaker.TeklaBridge.Extractor\ModelMaker.TeklaBridge.Extractor.csproj" -c Release -p:TeklaApiVersion=2025.0.0 --no-incremental --nologo
    if ($LASTEXITCODE -ne 0) { throw "Tekla exporter build failed" }

    $exe = "$root\src\ModelMaker.TeklaBridge.Extractor\bin\Release\net48\TeklaBridge.Extractor.exe"
    $out = 'C:\work\bridge-146-stage3d-probe'
    if (Test-Path $out) { throw "Output directory already exists" }
    & $exe --id 6813 --attributes-file "C:\work\bridge-146-attributes.txt" --output $out
    if ($LASTEXITCODE -ne 0) { throw "Live export failed" }

    $obj = @(Get-Content "$out\objects.json" -Raw | ConvertFrom-Json)
    $bg = $obj | Where-Object { $_.kind -eq 'BoltGroup' } | Select-Object -First 1
    if (-not $bg) { throw "Missing BoltGroup" }
    $bg.boltGroup.frameProbe | ConvertTo-Json -Depth 10
    $bg.boltGroup.holeSettingsProbe | ConvertTo-Json -Depth 10
    if ($bg.boltGroup.axis -or $bg.boltGroup.axisSource -or $bg.boltGroup.direction) {
        throw "Unsafe: unresolved axis should not be promoted"
    }
    py -3 tools\tekla_bridge_v31\verify_bridge31.py $out
    if ($LASTEXITCODE -ne 0) { throw "Package validation failed" }

    & '.\build-v31-noocc\model_maker_bridge_v31_package_test.exe' $out
    if ($LASTEXITCODE -ne 0) { throw "C++ reader failed" }

    & '.\build-v31-noocc\model_maker_bridge_auto_drill_policy_test.exe' $out
    if ($LASTEXITCODE -ne 0) { throw "Fail-closed policy failed" }

    & '.\build-v31-occ\model_maker_occ_bridge_v3_import_test.exe' $out
    if ($LASTEXITCODE -ne 0) { throw "Real OCC import failed" }

## Next gate
Compare exported frameProbe.normalCandidate and source BoltPositions against
the *physical* Tekla plate surface/hole geometry in the correct transformation
plane. Confirm per-ply hole type and size. Only after a separate, measured
authoritative geometry check may drilling permissions change.

NOTE: A unit normal vector is **not itself** a verified drilling axis.
