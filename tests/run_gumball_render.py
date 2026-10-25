#!/usr/bin/env python3
"""Compile the actual renderer gumball block against a recording GDI boundary.

Production statements, line() and DraftView gumball fields are extracted verbatim.
The test supplies an oblique projection fixture and records GDI calls, not pixels.
This is NOT a native Windows GUI, GDI rasterization or hit-testing test.
Usage: python3 tests/run_gumball_render.py [build-directory]
"""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
source = (root / "src/renderer.cpp").read_text()
header = (root / "include/model_maker/renderer.hpp").read_text()
start = source.index("    // RHINO TARZI GUMBALL:")
end = source.index("\n    if (!draft.interactiveNavigation) {", start)
block = source[start:end]
line_match = re.search(r"^void line\(.*?^}", source, re.M | re.S)
assert line_match, "Renderer line() helper not found"
line = line_match.group()
fields = "\n".join(re.findall(r"^    (?:bool|Vec3|double|int) gumball.*$", header, re.M))
assert len(fields.splitlines()) == 9, "DraftView gumball fields changed; review harness"
handles = header[header.index("enum class GumballHandle"):header.index("struct DraftView")]
template = (root / "tests/gumball_render_cases.cpp").read_text()
for marker, value in [("// @GUMBALL_FIELDS@", fields), ("// @GUMBALL_HANDLES@", handles),
                      ("// @LINE_FUNCTION@", line), ("// @GUMBALL_BLOCK@", block)]:
    assert template.count(marker) == 1, f"Missing/duplicate template marker: {marker}"
    template = template.replace(marker, value)
build.mkdir(parents=True, exist_ok=True)
generated = build / "gumball_render_generated.cpp"
binary = build / "gumball_render_test"
generated.write_text(template)
subprocess.run(["g++", "-std=c++20", "-O0", "-g", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "include"), str(generated), str(root / "src/geometry.cpp"),
                "-o", str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
