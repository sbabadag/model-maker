# Stage 3H — Independent DSTV NC1 ↔ Tekla live package parity

## Evidence received (user-supplied Tekla 2025 SP6 1001.nc1)

File SHA-256:
\`fbb4d98e0c6a0bd6913c634d6269273b96c8762300ca26e05bbc4e39649a8aac\`

- Header: part 1001, grade S235JR, type B plate, profile PL10,
  dimensions 200 x 170 x 10 mm.
- AK external contour closes at (0,0), (200,0), (200,170),
  (0,170), (0,0).
- Six round BO front-face records (NC U,V mm, diameter mm):
  (40,70,22), (40,130,22), (100,70,22), (100,130,22),
  (160,70,22), (160,130,22).
- No IK inner contour block (ordinary round holes are represented in BO,
  not necessarily IK).
- Through-bore theoretical net volume (if no other cuts):
  200*170*10 - 6*pi*(11)^2*10 = 317192.0373349381 mm^3.
- Prior source plate bbox (world mm): X=4.25..14.25,
  Y=195..365, Z=6959..7159.
  The six Tekla BoltPositions matched NC nominal local columns/rows by
  y_world=195+NC_V, z_world=6959+NC_U.
- NC1 has no Tekla sourceGUID: direct identity still must be proven
  with Tekla GetPartMark and GetDSTVCoordinateSystem; a dimension match
  alone must never silently authorize cutting.

DSTV BO syntax: face v = front; u/v dimensions are part-local; hole
diameter is the final numeric field here.
https://support.tekla.com/doc/tekla-structures/2021/int_create_nc_files
https://developer.tekla.com/doc/tekla-structures/2025/get-dstv-coordinate-system-method-53100

## Changes (read-only)

1. Adds optional plateHoleProbe.partMark via Part.GetPartMark() and
   plateHoleProbe.dstvCoordinateSystem via Part.GetDSTVCoordinateSystem()
   to the C# exporter; frame basis vectors normalized, no model modifications.
2. Adds \`compare_nc1_to_bridge.py\`:
   - parses ST, AK, BO (strictly round-hole syntax) and NC SHA-256
   - checks plate section profile/dimensions, target GUID link,
     hole count and nominal+tolerance diameter
   - compares model BoltPositions with independently projected DSTV points
     in the two dimensions transverse to the confirmed plate normal
   - checks projected NC centers sit within plate bbox and the
     NC face normal aligns with thickness direction
   - compares partMark against NC header identifiers if readable
   - reports drillingAuthorized=false, regardless of success
3. Adds four synthetic Python tests + one C# JSON round-trip test.
4. Does NOT embed the user's project NC1 in the public Git repository.
5. Does NOT change the Stage3C unsafe-drilling policy or generate
   fabricated holes. No automatic Model Maker native conversion.

## Windows PowerShell validation (real Tekla open)

    cd C:\work\model-maker-bridge-v31
    git fetch github
    git switch -c bridge-v31-stage3h-local --track github/tmp/bridge-v3.1-stage3h-dstv-parity-20261025
    $root = 'tools\model-maker-bridge\tekla-extractor'

    dotnet test "$root\tests\ModelMaker.TeklaBridge.Core.Tests\ModelMaker.TeklaBridge.Core.Tests.csproj" -c Release --nologo
    if ($LASTEXITCODE -ne 0) { throw 'C# tests failed' }
    dotnet build "$root\src\ModelMaker.TeklaBridge.Extractor\ModelMaker.TeklaBridge.Extractor.csproj" -c Release -p:TeklaApiVersion=2025.0.0 --no-incremental --nologo
    if ($LASTEXITCODE -ne 0) { throw 'Exporter build failed' }

    py -3 -m unittest discover -s tools\tekla_bridge_v31\tests -p test_compare_nc1_to_bridge.py -v
    if ($LASTEXITCODE -ne 0) { throw 'Python DSTV regression failed' }

    $exe = "$root\src\ModelMaker.TeklaBridge.Extractor\bin\Release\net48\TeklaBridge.Extractor.exe"
    $out = 'C:\work\bridge-146-stage3h-dstvframe'
    if (Test-Path $out) { throw 'Output dir already exists' }
    & $exe --id 6813 --attributes-file 'C:\work\bridge-146-attributes.txt' --output $out
    if ($LASTEXITCODE -ne 0) { throw 'Live export failed' }

    $p = (Get-Content "$out\objects.json" -Raw | ConvertFrom-Json) |
        Where-Object { $_.kind -eq 'ContourPlate' } | Select-Object -First 1
    $p.plateHoleProbe | ConvertTo-Json -Depth 7
    py -3 tools\tekla_bridge_v31\verify_bridge31.py $out
    if ($LASTEXITCODE -ne 0) { throw 'Bridge package invalid' }

    $model = 'C:\Users\Asus\Downloads\AYDIN_SALON1\New model'
    $nc = Get-ChildItem -LiteralPath $model -Filter '1001.nc1' -Recurse -File |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $nc) { throw 'Selected 1001.nc1 not found under original Tekla model' }
    py -3 tools\tekla_bridge_v31\compare_nc1_to_bridge.py $nc $out
    if ($LASTEXITCODE -ne 0) { throw 'NC1/Bridge parity failure: inspect printed diagnostics' }

Success confirms evidence consistency, not yet software equivalence nor
a release gate for real OCC hole creation. Stage 3C guard stays on.
Tests must actually run before calling Stage 3H validated.
