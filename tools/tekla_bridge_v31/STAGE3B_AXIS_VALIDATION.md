# Bridge V3.1 Stage 3B — Tekla coordinate-frame normalization

Based on actual live Tekla 2025 SP6 Bridge export of component 146:

- An exported axis pair measured **length 1000** each. A naive cross
  product yielded a third axis of **length 1,000,000**.
- The fitting-plane normal was length 490,000, not dimensionless.
- Exported plate dimensions from BBox: 10 x 170 x 200 mm.
- One M20 six-position bolt group still has an **unresolved bolt axis**,
  and its saved X positions do not intersect the plate's X bbox.
  **DO NOT drill holes** or silently shift bolt X.
- Legacy .j146 dialog file contained 624 settings; 75 filtered candidates
  were queried with GetAttribute, 65 readable, 10 unresolved.
  Most readable settings were generic/weld defaults, not geometric controls.

This branch fixes ONLY exporter frame units and cross-product normals.
Bridge JSON format remains "3.0"; no native generator, bolt coordinate
guessing, or fitted-solid boolean changes.

## Windows validation (Tekla 2025 SP6)

    cd C:\work\model-maker-bridge-v31
    git fetch github
    git switch -c bridge-v31-stage3b-local --track github/tmp/bridge-v3.1-stage3b-axis-fix-20261025

    $root = "tools\model-maker-bridge\tekla-extractor"
    dotnet test "$root\tests\ModelMaker.TeklaBridge.Core.Tests\ModelMaker.TeklaBridge.Core.Tests.csproj" -c Release --nologo
    if ($LASTEXITCODE -ne 0) { throw "Core C# tests failed" }

    dotnet build "$root\src\ModelMaker.TeklaBridge.Extractor\ModelMaker.TeklaBridge.Extractor.csproj" -c Release -p:TeklaApiVersion=2025.0.0 --no-incremental --nologo
    if ($LASTEXITCODE -ne 0) { throw "Tekla exporter build failed" }

When Tekla 2025 SP6 model is open, use the *live* component ID (check
list-components --json again if the model or components have changed):

    $exe = "$root\src\ModelMaker.TeklaBridge.Extractor\bin\Release\net48\TeklaBridge.Extractor.exe"
    & $exe list-components --json
    $out = "C:\work\bridge-146-axisfix-test"
    if (Test-Path $out) { throw "Select an unused output directory" }
    & $exe --id 6813 --output $out
    if ($LASTEXITCODE -ne 0) { throw "Live Tekla export failed" }

    $frame = Get-Content "$out\component.json" -Raw | ConvertFrom-Json
    $frame.coordinateSystem | ConvertTo-Json -Depth 5
    $objects = Get-Content "$out\objects.json" -Raw | ConvertFrom-Json
    $objects | Where-Object kind -eq "Fitting" | ForEach-Object { $_.fitting.plane.normal | ConvertTo-Json }
    py -3 tools\tekla_bridge_v31\verify_bridge31.py $out

Expected normalized axes for this orientation:
  X=(0,1,0), Y=(0,0,1), Z=(1,0,0), fitting normal=(0,-1,0).
Test actual values; do not claim passing prior to Windows validation.

Do NOT switch the build directory to OCC-on, do NOT merge to main,
and do NOT modify the original Tekla model.
