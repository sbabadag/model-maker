# Stage 3E: hole-option probe for live Tekla 146 (read-only)

Validated Stage 3D live package:
- BoltGroup origin=(0,295,7119), xAxis=(0,0,-1), yAxis=(0,1,0),
  zAxis=(1,0,0), normalCandidate=(1,0,0) but verifiedForDrilling=false.
- plainHoleType=HOLE_TYPE_THROUGH, specialHoleType=HOLE_TYPE_SLOTTED,
  all hole1..hole5 flags false, slotted allowances 0; 6 M20 positions.
- The source plate GUID matches the bolt group's partToBoltTo GUID.
- Former live plate GetSolid returned 6 faces, 6 loops, holeLoops=0;
  that alone does NOT prove absent physical bolt holes (API limitations).
- Official Tekla help: these five checkboxes choose parts with **special**
  holes (oversized/slotted/etc). Unchecked can still mean ordinary ROUND
  holes. For round holes, nominal diameter=bolt size+tolerance (20+2=22 mm).
- HoleType=SLOTTED without any special ply flags is not itself proof of
  slotted physical holes.

Change: read-only probe exports BoltGroup.Bolt boolean (bolt vs hole only)
and BoltGroup.BoltHolesAttributes[] individual special-hole settings.
No drill authorization changes. Stage 3C fails closed.

Windows verification:

    cd C:\work\model-maker-bridge-v31
    git fetch github
    git switch -c bridge-v31-stage3e-local --track github/tmp/bridge-v3.1-stage3e-hole-metadata-20261025
    $root = 'tools\model-maker-bridge\tekla-extractor'
    dotnet test "$root\tests\ModelMaker.TeklaBridge.Core.Tests\ModelMaker.TeklaBridge.Core.Tests.csproj" -c Release --nologo
    if ($LASTEXITCODE -ne 0) { throw 'C# unit test error' }
    dotnet build "$root\src\ModelMaker.TeklaBridge.Extractor\ModelMaker.TeklaBridge.Extractor.csproj" -c Release -p:TeklaApiVersion=2025.0.0 --no-incremental --nologo
    if ($LASTEXITCODE -ne 0) { throw 'Tekla exporter build error' }
    $exe = "$root\src\ModelMaker.TeklaBridge.Extractor\bin\Release\net48\TeklaBridge.Extractor.exe"
    $out = 'C:\work\bridge-146-stage3e-probe'
    if (Test-Path $out) { throw 'Output already exists' }
    & $exe --id 6813 --attributes-file 'C:\work\bridge-146-attributes.txt' --output $out
    if ($LASTEXITCODE -ne 0) { throw 'Live Tekla exporter failed' }
    $objects = Get-Content "$out\objects.json" -Raw | ConvertFrom-Json
    $bg = $objects | Where-Object { $_.kind -eq 'BoltGroup' } | Select-Object -First 1
    $bg.boltGroup.holeSettingsProbe | ConvertTo-Json -Depth 8
    py -3 tools\tekla_bridge_v31\verify_bridge31.py $out
    if ($LASTEXITCODE -ne 0) { throw 'Verification failed' }
    & .\build-v31-noocc\model_maker_bridge_auto_drill_policy_test.exe $out
    if ($LASTEXITCODE -ne 0) { throw 'Unsafe drill policy' }

Expected: C# 32/32, new boltPresent/perPlySpecialHoles metadata visible
if available, packageValid=true and automatic drill allowed=false.
Actual 2025 SP6 compile and live output remain unverified until user tests.

Official refs:
https://support.tekla.com/doc/tekla-structures/2026/mod_holes
https://developer.tekla.com/doc/tekla-structures/2025/bolt-holes-attributes-property-52077
https://developer.tekla.com/doc/tekla-structures/2025/bolt-hole-attributes-properties-52130
