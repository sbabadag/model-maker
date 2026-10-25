#!/usr/bin/env python3
"""Verify the snap marker size (2x) and that markers are drawn from ONE source.

Two layers:
  1. Source shape: a single drawSnapMarkerShape() definition exists, BOTH render
     paths call it, the duplicate switch blocks are gone, the label offset scales
     with the marker, and the scale constant is 2.
  2. Behaviour: the production helper block is extracted verbatim from
     renderer.cpp and compiled against a recording GDI boundary (no pixels); the
     recorded primitives must equal the historical 1x geometry multiplied by two,
     for every SnapMarkerSymbol.
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
helper_fn = source.index("void drawSnapMarkerShape(HDC dc, const POINT& p, SnapMarkerSymbol symbol) {",
                         helper_start)
helper_end = source.index("\n}\n", helper_fn) + len("\n}\n")
block = source[helper_start:helper_end]

check(source.count("void drawSnapMarkerShape(") == 1,
      "exactly ONE snap marker renderer exists (no duplicated switch)")
check(source.count("switch (snapMarkerSymbol(draft.snapType)) {") == 0,
      "the two duplicated marker switch blocks are gone")
check(source.count("drawSnapMarkerShape(dc, p, snapMarkerSymbol(draft.snapType));") == 2,
      "BOTH render paths (motion overlay + normal drafting) call the single helper")
check(re.search(r"constexpr int kSnapMarkerScale = 2;", block) is not None,
      "marker scale is a single named constant = 2")
check("kSnapMarkerScale" in block and "sm(" in block,
      "the helper scales every offset through the shared factor")
check(source.count("p.x - 7") == 0 and source.count("p.y - 7") == 0,
      "no unscaled marker coordinate remains in the renderer")
check(source.count("drawText(dc, p.x + sm(10), p.y + sm(8), snapTypeLabel") == 2,
      "the snap label sits outside the enlarged marker (offset scaled too)")
check(re.search(r"CreatePen\(PS_SOLID, 2,\s*\n?\s*RGB", source) is not None,
      "pen width is untouched (user asked for size, not line weight)")

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
