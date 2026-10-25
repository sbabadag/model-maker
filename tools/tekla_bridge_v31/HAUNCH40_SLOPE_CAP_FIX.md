# Haunch 40: inclined-flange contact and column-top cap fix

Base: \`tmp/haunch40-bolt-placement-hotfix-20261025\`
Branch: \`tmp/haunch40-slope-column-cap-fix-20261025\`
main untouched.

## Root cause & correction

**Sloped lower flange**: the web is a rectangular L x D plate with a
corner-1 chamfer h=L-25, v=D-tip. The exact chamfer line starts at
local (u=-L/2+25, v=-D/2) and ends at (u=+L/2,v=+D/2-tip).
Old lower flange incorrectly started at u=-L/2, giving a visibly
different slope. It was also centered on the cut line rather than
laying its inner face against the web edge.

New code extracts BOTH endpoints from the SAME actual web chamfer and
sets flange.v = normalize(far-near), flange.axis = normalize(webWidth
CROSS flange.v), and flange.center = midpoint + flange.axis * t/2.
Thus the inner face (center - t/2 along normal) meets the line at both
ends. This is a real physical BRep contact, independent of beam axis
sign/orientation.

**Column cap**: the old "Top Plate" was on the top flange of the beam,
not the column. Now a true horizontal cap is built from the physically
higher endpoint of the selected COLUMN. The source SectionAnchor
offset is included, and the plate's lower face is at the column top.
Its minimum plan dimensions cover the real selected column footprint
with a 10 mm margin each side. For sloping beams the cap stays horizontal.

The existing editable schema keys topPlateThickness/topPlateLength
remain unchanged (MMW8/undo/recompute compatibility), but the dialog
now has a dedicated "Kolon Cap" tab. The cap's weld references the
COLUMN rather than the beam. The same semantic child keys stay intact
for Document recompute and previously stored instances.

**No changes** to the earlier M20 bolt stack/local placement hotfix.

## Windows PowerShell

    cd C:\work\model-maker-bridge-v31
    Get-Process "model-maker" -ErrorAction SilentlyContinue | Stop-Process -Force
    git fetch github
    git switch -c haunch40-cap-fix-local --track github/tmp/haunch40-slope-column-cap-fix-20261025

    cmake -S . -B build-v31-occ -DMM_OCC_LINK_DIRECT=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure error' }

    cmake --build build-v31-occ --target model_maker_component_engine_test model_maker_component_dialog_test model_maker_occ_haunch40_native_e2e_test model-maker --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Haunch40 build error' }

    ctest --test-dir build-v31-occ -R '^model_maker_(component_engine|component_dialog|occ_haunch40_native_e2e)_test$' --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Haunch40 tests failed' }

    $env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
    & '.\build-v31-occ\model-maker.exe'

For visual acceptance, work on a copy of the model, DELETE or undo the
previous wrongly created Haunch40 component and create a NEW one.
Old Document geometry is not rewritten by replacing the EXE until
an explicit recompute. Confirm the cap rests at the actual column top
and the lower inclined flange is parallel AND coincident to the tapered
web's edge from one endpoint to the other.

The new tests include: exact chamfer endpoints, inner-face contact,
column-top cap position/sizing, reversed column axis, sloped secondary
beam, column weld target and OCC cap BRep bounding box.

Actual Windows/OCCT compilation and test results require user execution;
do not label this hotfix verified before running them.
