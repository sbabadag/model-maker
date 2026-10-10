#!/usr/bin/env python3
"""Verify the work-plane ghost renderer.

Two layers:
  1. Source shape: the ghost plane is drawn by every viewport render path, is
     render-only (no selection/snap/document access) and the F7 toggle is wired
     exactly once through both window procs.
  2. Behaviour: the production ghost-plane block is extracted verbatim from
     renderer.cpp and compiled against a recording GDI boundary (no pixels).
This is NOT a native Windows GUI, GDI rasterization or hit-testing test.
Usage: python3 tests/run_workplane_ghost.py [build-directory]
"""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
source = (root / "src/renderer.cpp").read_text()
header = (root / "include/model_maker/renderer.hpp").read_text()
app_source = (root / "src/application.cpp").read_text()
app_header = (root / "include/model_maker/application.hpp").read_text()

failures = []


def check(ok, label):
    print(("PASS " if ok else "FAIL ") + label)
    if not ok:
        failures.append(label)


marker = "        // CALISMA DUZLEMI HAYALETI (ghost work plane)"
start = source.index(marker)
end = source.index("\n    }; // drawGridAndAxes", start)
block = source[start:end]
check(source.index("const auto drawGridAndAxes = [&](HDC targetDc) {") < start,
      "ghost plane block lives inside drawGridAndAxes")
check(source.count("drawGridAndAxes(dc);") >= 2,
      "drawGridAndAxes runs in both the GDI and the GL composite path (every viewport)")
for token in ("document", "selectedModels", "gumball", "snapType", "updateHover", "hover_", "modelBounds"):
    check(token not in block, f"render-only: '{token}' absent from ghost block (non-selectable/non-snappable)")
check(re.search(r"draft\.[A-Za-z_]+\s*=", block) is None, "ghost block never mutates view state")
check("bool workPlaneGhostVisible{true};" in header, "DraftView carries the ghost flag")
check("view.workPlaneGhostVisible = workPlaneGhostVisible_;" in app_source,
      "draftView() propagates the flag to every canvas")
check("void toggleWorkPlaneGhost();" in app_header, "toggle declared in the Application API")
check("void Application::toggleWorkPlaneGhost()" in app_source, "toggle implemented once for key and menu")
check(app_source.count("VK_F7") == 2, "F7 handled in both the canvas and the outer window proc")

line_match = re.search(r"^void line\(.*?^}", source, re.M | re.S)
assert line_match, "Renderer line() helper not found"
template = (root / "tests/workplane_ghost_cases.cpp").read_text()
for hook, value in [("// @LINE_FUNCTION@", line_match.group()), ("// @GHOST_BLOCK@", block)]:
    assert template.count(hook) == 1, f"Missing/duplicate template marker: {hook}"
    template = template.replace(hook, value)
build.mkdir(parents=True, exist_ok=True)
generated = build / "workplane_ghost_generated.cpp"
binary = build / "workplane_ghost_test"
generated.write_text(template)
subprocess.run(["g++", "-std=c++20", "-O0", "-g", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "include"), str(generated), str(root / "src/geometry.cpp"),
                "-o", str(binary)], check=True)
behaviour = subprocess.run([str(binary)]).returncode

if failures:
    print(f"Work plane ghost: {len(failures)} source-shape check(s) failed")
if behaviour or failures:
    sys.exit(1)
print("Work plane ghost: source shape and behaviour verified")
