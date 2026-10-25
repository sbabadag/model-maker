# Stage 4 — Create editable Tekla 146 component from source + DSTV/NC1

**This is a real native component-creation workflow, not another API probe.**
The Model Maker menu already has "Native Component'e Dönüştür..." and its
production runtime already supports \`tekla_shear_plate_146\`, the component
generator, materializer, owned plate + bolts, undo/redo and recomputation.

New workflow on a **separate experimental branch**:
1. In Model Maker create/select existing source-matched HEA400 column and
   HEA300 beam (must be real Document entities, order of selection immaterial).
2. Choose \`Native Component'e Dönüştür...\`.
3. Pick the NEW Stage3H Bridge V3 package directory exported from Tekla
   connection ID 6813; it must contain a measured plate DSTV frame and part mark.
4. Choose original Tekla-exported \`1001.nc1\` when second file dialog opens.
5. The importer validates: connection=146, exact plate target GUID,
   NC plate profile/material/10x170x200 size, optional part mark,
   measured DSTV frame normal along plate thickness, six round Ø22 BO holes,
   all NC centers within plate and uniquely matched to source bolt positions.
6. Only in the in-memory BridgePackage copy, hole positions are projected to
   the physical PL10 centre plane and marked as NC-measured.
7. Existing \`convertBridgeToNative()\` then constructs an actual editable
   \`tekla_shear_plate_146\` on the selected Model Maker members.
8. If any source match fails, NO component, hole or Document edit is made.
   The original Tekla exporter package and the NC1 file are unchanged.

The source sample NC1 was supplied by the user, not included in GitHub:
- ST S235JR, PL10, B, 200 x 170 x 10 mm
- BO six circular through Ø22 mm holes at
  (u,v)=(40,70),(40,130),(100,70),(100,130),(160,70),(160,130)
- AK rectangular plate external contour (5 points), no IK.
- NC U corresponds to observed Tekla Z, NC V to observed Tekla Y
  for this particular plate/frame. This is checked from the actual
  \`Part.GetDSTVCoordinateSystem()\` for each source package.

## Windows build / acceptance

    cd C:\work\model-maker-bridge-v31
    git fetch github
    git switch -c bridge-v31-stage4-local --track github/tmp/bridge-v3.1-stage4-native-146-20261025
    cmake --build build-v31-noocc --target model_maker_bridge_nc146_test --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'NC1 parser build failed' }
    & '.\build-v31-noocc\model_maker_bridge_nc146_test.exe'
    if ($LASTEXITCODE -ne 0) { throw 'NC1 source tests failed' }

    cmake --build build-v31-occ --target model_maker_occ_bridge_native_convert_test --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Native OCC build failed' }
    ctest --test-dir build-v31-occ -R '^model_maker_occ_bridge_native_convert_test$' --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Native OCC acceptance failed' }

    # Build GUI on your existing Qt+OCC build directory (if target exists).
    cmake --build build-v31-occ --target model-maker --parallel 4

Important: the GUI build may be in a separate Qt/vcpkg directory. Use that
known OCC-enabled Qt build directory if target \`model-maker\` is absent.

For the live package, first build the Stage3H exporter and re-export
\`C:\work\bridge-146-stage4-source\` from Tekla 2025 SP6:
    $root='tools\model-maker-bridge\tekla-extractor'
    dotnet build "$root\src\ModelMaker.TeklaBridge.Extractor\ModelMaker.TeklaBridge.Extractor.csproj" -c Release -p:TeklaApiVersion=2025.0.0 --no-incremental --nologo
    $exe="$root\src\ModelMaker.TeklaBridge.Extractor\bin\Release\net48\TeklaBridge.Extractor.exe"
    & $exe --id 6813 --attributes-file 'C:\work\bridge-146-attributes.txt' --output 'C:\work\bridge-146-stage4-source'
    $p=(Get-Content 'C:\work\bridge-146-stage4-source\objects.json' -Raw | ConvertFrom-Json) | Where-Object kind -eq ContourPlate | Select-Object -First 1
    $p.plateHoleProbe | ConvertTo-Json -Depth 5

The native 146 generator still does NOT guarantee feature-for-feature
Tekla equivalence: secondary fitting operations, proprietary 146 tuning
attributes and some weld options remain unsupported. This stage creates a
functional editable native 146 with verified plate dimensions + hole pattern.
Do not merge to main until OCC-on Windows build/acceptance passes.

Known source example: HEA400 primary + HEA300 secondary.
