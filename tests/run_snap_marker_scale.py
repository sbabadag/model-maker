#!/usr/bin/env python3
"""Verify the snap marker scale (configurable, default 3x) and single-source drawing.

UI-6a: the marker scale is no longer a compile-time constant (kSnapMarkerScale=2)
but a runtime `int scale` fed from Application::SnapSettings (markerScale default 3
= 1.5x of the old 2x), and the pen width is SnapSettings.markerPenWidth (default 3).

Two layers:
  1. Source shape: a single drawSnapMarkerShape() definition exists, BOTH render
     paths call it with the runtime scale (draft.snapMarkerScale), the pen width is
     configurable (draft.snapMarkerPenWidth), the duplicate switch blocks are gone,
     and the label offset scales with the marker.
  2. Behaviour: the production helper block is extracted verbatim from renderer.cpp
     and compiled against a recording GDI boundary (no pixels); the recorded
     primitives must equal the historical 1x geometry multiplied by the requested
     scale, for every SnapMarkerSymbol and every scale in {1,2,3,4}.
This is NOT a native Windows GUI or GDI rasterization test.
Usage: python3 tests/run_snap_marker_scale.py [build-directory]
"""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
source = (root / "src/renderer.cpp").read_text()

failures = []


def check(ok, label):
    print(("PASS " if ok else "FAIL ") + label)
    if not ok:
        failures.append(label)


# --- Source shape -------------------------------------------------------------
helper_start = source.index("// Snap isaretcisi cizimi: TEK kaynak")
helper_fn = source.index(
    "void drawSnapMarkerShape(HDC dc, const POINT& p, SnapMarkerSymbol symbol, int scale) {",
    helper_start)
helper_end = source.index("\n}\n", helper_fn) + len("\n}\n")
block = source[helper_start:helper_end]

check(source.count("void drawSnapMarkerShape(") == 1,
      "exactly ONE snap marker renderer exists (no duplicated switch)")
check(source.count("switch (snapMarkerSymbol(draft.snapType)) {") == 0,
      "the two duplicated marker switch blocks are gone")
check(source.count("drawSnapMarkerShape(dc, p, snapMarkerSymbol(draft.snapType), "
                   "draft.snapMarkerScale);") == 2,
      "BOTH render paths (motion overlay + normal drafting) call the single helper "
      "with the runtime scale")
check(re.search(r"void drawSnapMarkerShape\(HDC dc, const POINT& p, "
                r"SnapMarkerSymbol symbol, int scale\) \{", block) is not None,
      "marker scale is a runtime int scale parameter (no compile-time constant)")
check("int scale" in block and "sm(" in block and "v * scale" in block,
      "the helper scales every offset through the shared scale factor")
check(source.count("p.x - 7") == 0 and source.count("p.y - 7") == 0,
      "no unscaled marker coordinate remains in the renderer")
check(source.count("drawText(dc, p.x + 10 * draft.snapMarkerScale, "
                   "p.y + 8 * draft.snapMarkerScale,") == 2,
      "the snap label sits outside the enlarged marker (offset scaled too)")
check(source.count("CreatePen(PS_SOLID, draft.snapMarkerPenWidth,") == 2,
      "pen width is configurable (SnapSettings.markerPenWidth) in BOTH paths")

for symbol in ("Triangle", "Circle", "CrossedCircle", "Diamond", "BoxedCross",
               "ExtensionLine", "LinkedSquares", "RightAngle", "TangentCircle",
               "Hourglass", "ParallelLines", "GridCross", "Square"):
    check(f"SnapMarkerSymbol::{symbol}" in block, f"helper keeps symbol {symbol}")
check(block.count("case SnapMarkerSymbol::") == 16,
      "helper covers every SnapMarkerSymbol enumerator")

# --- Behaviour: extracted production body against a recording GDI boundary ----
line_match = re.search(r"^void line\(.*?^}", source, re.M | re.S)
assert line_match, "Renderer line() helper not found"
template = (root / "tests/snap_marker_cases.cpp").read_text()
for hook, value in [("// @LINE_FUNCTION@", line_match.group()), ("// @HELPER_BLOCK@", block)]:
    assert template.count(hook) == 1, f"Missing/duplicate template marker: {hook}"
    template = template.replace(hook, value)
build.mkdir(parents=True, exist_ok=True)
generated = build / "snap_marker_generated.cpp"
binary = build / "snap_marker_test"
generated.write_text(template)
subprocess.run(["g++", "-std=c++20", "-O0", "-g", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "include"), str(generated), "-o", str(binary)], check=True)
behaviour = subprocess.run([str(binary)]).returncode

if failures:
    print(f"Snap marker scale: {len(failures)} source-shape check(s) failed")
if behaviour or failures:
    sys.exit(1)
print("Snap marker scale: source shape and behaviour verified")
