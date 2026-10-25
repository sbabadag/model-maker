# Bridge V3.1 Stage 3A — Tekla 2025 Open API exporter (attribute-list support)

**Base:** Stage 2 (C++ Bridge V3 reader). This worktree only. No change to
PERF-003A, Hotfix-01 or main. **Do not merge into main.**

## What changed

Existing C# Tekla exporter now accepts --attributes-file, containing *known raw*
Tekla component attribute names. A line may contain one name or comma, semicolon
or whitespace separated names. Full-line # / // comments are allowed.
Duplicates are removed in input order, including names repeated via --attributes.
Malformed / missing files fail before attempting a Tekla connection.

Example text file (replace these with *actual confirmed* Tekla raw names):
  # example only, do not assume these exist in Tekla 146
  tpl1
  bolt_d
  e1

The list is **not** an .inp file. .inp is a Tekla dialog configuration
and is not exhaustively equivalent to component GetAttribute parameters.
Never treat successful list parsing as successful Tekla parameter reading.

## Windows PowerShell

From an existing test worktree:

    cd C:\work\model-maker-bridge-v31
    git fetch github
    git switch -c bridge-v31-stage3-local --track github/tmp/bridge-v3.1-stage3-exporter-20261025

    $root = "tools\model-maker-bridge\tekla-extractor"
    dotnet test "$root\tests\ModelMaker.TeklaBridge.Core.Tests\ModelMaker.TeklaBridge.Core.Tests.csproj" -c Release --nologo
    if ($LASTEXITCODE -ne 0) { throw "C# unit tests failed" }

    dotnet build "$root\src\ModelMaker.TeklaBridge.Extractor\ModelMaker.TeklaBridge.Extractor.csproj" -c Release -p:TeklaVersion=2025.0 --nologo
    if ($LASTEXITCODE -ne 0) { throw "Tekla exporter build failed" }

After Tekla 2025 is OPEN and an actual model loaded, list live components:

    $exe = "$root\src\ModelMaker.TeklaBridge.Extractor\bin\Release\net48\TeklaBridge.Extractor.exe"
    & $exe list-components --json

Select the target by its **current** Tekla ID from that output, then create
a raw attribute names file containing actual verified names (not .inp syntax):

    $attrs = "C:\work\bridge-146-attributes.txt"
    @("YOUR_CONFIRMED_RAW_ATTRIBUTE_NAME") | Set-Content -Encoding UTF8 $attrs
    & $exe --attributes-file $attrs --output C:\work\bridge-146-test

With a single selected Tekla component, no ID is needed. If selecting by ID, use the live ID from list-components; do not reuse IDs from older probe sessions.
The output is a V3 (manifest.formatVersion=3.0) package, not a parametric
connection. Inspect parameters.json, diagnostics.json, and checksums.json,
then test with Model Maker's existing Bridge importer. Unknown names must show
as unresolved, not silently defaulted.

## Verification scope

- The previous C++ tests were reported 4/4 passing on Windows OCC-off.
- The new C# tests and Tekla 2025 build have **not yet been run** in this
  environment (no .NET SDK / Tekla installation available here).
- No real Tekla 146/144 attribute list has yet been supplied or verified.
- Exact BRep geometry, bolt-hole axes, weld solids, and native editability
  remain UNVERIFIED and are out of scope for Stage 3A.
