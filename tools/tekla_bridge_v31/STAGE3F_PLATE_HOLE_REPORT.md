# Bridge Stage 3F — Source plate HAS_HOLES + RAW/NORMAL topology (read-only)

Input: live Tekla Structures 2025 SP6 Shear plate simple 146.
Prior Stage 3E user tests: C# 32/32, exporter build 0 errors/warnings,
packageValid=true. boltPresent=true, Hole1..Hole5=false,
plainHoleType=HOLE_TYPE_THROUGH, all special slotted offsets=0.
Measured group normalCandidate=+X, plate thickness direction X,
but source bolt positions X=0 while plate X bounds=4.25..14.25.

Official source:
- Tekla 2025 part HAS_HOLES report attribute returns 1 when bolt holes
  exist in the part, else 0; it does NOT cover ordinary cuts:
  https://support.tekla.com/doc/tekla-structures/2025/has_holes
- ModelObject.GetReportProperty(name, ref int) overload:
  https://developer.tekla.com/doc/tekla-structures/2025/get-report-property-method-string-int32-53010
- Part.GetSolid(Solid.SolidCreationTypeEnum):
  RAW=no boolean operations, NORMAL=cuts/fittings included:
  https://developer.tekla.com/doc/tekla-structures/2025/solid-solid-creation-type-enum-enumeration-53823

Stage 3F adds optional \`objects.json[].plateHoleProbe\` for ContourPlate:
- reportReadable, reportValue, hasBoltHoles (nullable, never guessed)
- rawTopology / normalTopology counts, topologyDiffers (nullable)
- verifiedHoleCount is always null
- verifiedForDrilling is always false

This does NOT automatically count physical holes nor prove their axes,
diameters, centres, shape, or correspondence to BoltPositions.
Hole policy from Stage 3C stays fail-closed and unchanged.
If HAS_HOLES=1 but topologies appear identical, do NOT override HAS_HOLES
with the topology heuristic; report the inconsistency for inspection.
Package format remains 3.0; unknown optional JSON fields ignored by
existing C++ importer.

### Windows checks (new branch, no main changes)

    cd C:\work\model-maker-bridge-v31
    git fetch github
    git switch -c bridge-v31-stage3f-local --track github/tmp/bridge-v3.1-stage3f-plate-hole-report-20261025

    $root = 'tools\model-maker-bridge\tekla-extractor'
    dotnet test "$root\tests\ModelMaker.TeklaBridge.Core.Tests\ModelMaker.TeklaBridge.Core.Tests.csproj" -c Release --nologo
    if ($LASTEXITCODE -ne 0) { throw 'C# tests failed' }

    dotnet build "$root\src\ModelMaker.TeklaBridge.Extractor\ModelMaker.TeklaBridge.Extractor.csproj" -c Release -p:TeklaApiVersion=2025.0.0 --no-incremental --nologo
    if ($LASTEXITCODE -ne 0) { throw 'Exporter failed to build' }

    $exe = "$root\src\ModelMaker.TeklaBridge.Extractor\bin\Release\net48\TeklaBridge.Extractor.exe"
    $out = 'C:\work\bridge-146-stage3f-holereport'
    if (Test-Path $out) { throw 'Output dir exists' }
    & $exe --id 6813 --attributes-file 'C:\work\bridge-146-attributes.txt' --output $out
    if ($LASTEXITCODE -ne 0) { throw 'Live exporter failed' }

    $objects = @(Get-Content "$out\objects.json" -Raw | ConvertFrom-Json)
    $plate = $objects | Where-Object { $_.kind -eq 'ContourPlate' } | Select-Object -First 1
    if (-not $plate) { throw 'No plate in live package' }
    $plate.plateHoleProbe | ConvertTo-Json -Depth 8

    py -3 tools\tekla_bridge_v31\verify_bridge31.py $out
    if ($LASTEXITCODE -ne 0) { throw 'Invalid exported package' }

    & '.\build-v31-noocc\model_maker_bridge_auto_drill_policy_test.exe' $out
    if ($LASTEXITCODE -ne 0) { throw 'Policy regression failed' }

Expected new core suite 33/33 and, after live export, plateHoleProbe data.
These are **targets** until the user runs the commands; do not mark passed.
