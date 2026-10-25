# Stage 3G — Native Tekla hole-type report probes (read-only)

Source live 146 (Stage 3F):
- C# test suite 33/33, exporter 0 warnings/errors; packageValid=true.
- ContourPlate.HAS_HOLES=1, confirming >=1 bolt hole on the part.
- RAW and NORMAL solid topology counters both faces=6, loops=6;
  equality of these counters does not disprove HAS_HOLES or prove hole count.
- 6 x M20 bolt positions, boltPresent=true, normalCandidate=(+1,0,0),
  plainHoleType=HOLE_TYPE_THROUGH, 5 special-hole flags false.
- Do **not** authorize physical drilling in the Stage3C importer yet.

Stage 3G adds optional BoltGroup.holeSettingsProbe.reportProbes[].
Probes: HOLE_1_TYPE..HOLE_5_TYPE, PLAIN_HOLE_TYPE,
DIAMETER_X, DIAMETER_Y via model GetReportProperty.
Each record has readable=true with exact returned string or readable=false
with no fabricated value. Template attributes may not be exposed for a
BoltGroup ModelObject: **an unreadable field is not an error**.

Reference (Tekla 2025): HOLE_1_TYPE..HOLE_5_TYPE values:
  -1 ordinary, 0 slotted, 1 oversized, 2 no hole, 3 tapped.
This is per-ply, not a count of 6 bolt positions.
https://support.tekla.com/cs/doc/tekla-structures/2025/hole_1_5_type

When GetReportProperty does not expose these fields, independent
fabrication proof is a single-plate DSTV/NC export: BO blocks represent
holes in Tekla-produced DSTV files. Do NOT treat the NC file as already
present. It must be created explicitly and matched to the same plate GUID.
https://support.tekla.com/doc/tekla-structures/2021/int_create_nc_files

### Windows smoke

    cd C:\work\model-maker-bridge-v31
    git fetch github
    git switch -c bridge-v31-stage3g-local --track github/tmp/bridge-v3.1-stage3g-hole-type-reports-20261025
    $root = "tools\model-maker-bridge\tekla-extractor"
    dotnet test "$root\tests\ModelMaker.TeklaBridge.Core.Tests\ModelMaker.TeklaBridge.Core.Tests.csproj" -c Release --nologo
    if ($LASTEXITCODE -ne 0) { throw "Core tests failed" }
    dotnet build "$root\src\ModelMaker.TeklaBridge.Extractor\ModelMaker.TeklaBridge.Extractor.csproj" -c Release -p:TeklaApiVersion=2025.0.0 --no-incremental --nologo
    if ($LASTEXITCODE -ne 0) { throw "Exporter build failed" }

    $exe = "$root\src\ModelMaker.TeklaBridge.Extractor\bin\Release\net48\TeklaBridge.Extractor.exe"
    $out = "C:\work\bridge-146-stage3g-holetypes"
    if (Test-Path $out) { throw "Output dir already exists" }
    & $exe --id 6813 --attributes-file "C:\work\bridge-146-attributes.txt" --output $out
    if ($LASTEXITCODE -ne 0) { throw "Export failed" }

    $objects = Get-Content "$out\objects.json" -Raw | ConvertFrom-Json
    $bg = $objects | Where-Object kind -eq "BoltGroup" | Select-Object -First 1
    $bg.boltGroup.holeSettingsProbe.reportProbes | Format-Table name,readable,value -AutoSize
    $plate = $objects | Where-Object kind -eq "ContourPlate" | Select-Object -First 1
    $plate.plateHoleProbe | ConvertTo-Json -Depth 6

    py -3 tools\tekla_bridge_v31\verify_bridge31.py $out
    if ($LASTEXITCODE -ne 0) { throw "Package failed verification" }
    & '.\build-v31-noocc\model_maker_bridge_auto_drill_policy_test.exe' $out
    if ($LASTEXITCODE -ne 0) { throw "Drill safety regression failed" }

Targets: 34/34 C# tests, exporter compiles in Tekla 2025 SP6,
valid package and live auto-drill denied. Not yet Windows-tested.
This branch must not alter main or promote unverified drilling.
