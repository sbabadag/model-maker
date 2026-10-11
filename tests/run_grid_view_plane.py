#!/usr/bin/env python3
"""Tekla grid gorunus-duzlemi katmanini dogrular.

Kullanici sarti: "grid de pencerenin duzlemine gore sadece xy yz xz gridleri
gozukur 3d grid gozukmez". Tekla dokumani (Work with grids / Create a grid):
grid bir 3B DUZLEMLER KOMPLEKSIDIR; bir gorunuste YALNIZCA gorunus duzlemini
KESEN grid duzlemlerinin kesisim cizgileri cizilir ("The grid is shown on the
view plane using dash-and-dot lines"; "The grid is only shown in the view if
any of the grids' planes intersect with the view plane"). Gorunus duzlemine
PARALEL olan aile cizilmez.

Iki katman:
  1. Kaynak sekli: cizim `gridAxesForView` uzerinden ve gorunus duzlemi
     kameradan geliyor; `grid.axes` cizimde KULLANILMIYOR (o snap/zoom
     tabani); `rebuildGridAxes` ile `gridAxesForView(XY)` tek kaynak.
  2. Davranis: uretim grid cizim blogu renderer.cpp'den VERBATIM cikarilip
     GERCEK `gridAxesForView`/`gridViewPlaneForDirection` (src/grid.cpp) ile
     sahte kaydedici GDI sinirinda derlenir; pencerenin duzlemine gore hangi
     ailelerin/etiketlerin cizildigi olculur.

Bu native Windows GUI / GDI rasterizasyon testi DEGILDIR.
Kullanim: python3 tests/run_grid_view_plane.py [build-directory]
"""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
source = (root / "src/renderer.cpp").read_text(encoding="utf-8")
header = (root / "include/model_maker/grid.hpp").read_text(encoding="utf-8")
grid_source = (root / "src/grid.cpp").read_text(encoding="utf-8")

failures = []


def check(ok, label):
    print(("PASS " if ok else "FAIL ") + label)
    if not ok:
        failures.append(label)


# --- 1) Kaynak sekli ---------------------------------------------------------
marker = "        // TEKLA-TARZI YAPI GRIDI:"
start = source.index(marker)
end = source.index("        // CALISMA DUZLEMI HAYALETI (ghost work plane)", start)
block = source[start:end]

check(source.index("const auto drawGridAndAxes = [&](HDC targetDc) {") < start,
      "grid block lives inside drawGridAndAxes")
check(source.count("drawGridAndAxes(dc);") >= 2,
      "drawGridAndAxes runs in both the GDI and the GL composite path (every viewport)")
check("gridAxesForView(grid, gridPlane)" in block,
      "drawing uses gridAxesForView(grid, gridPlane)")
check("gridViewPlaneForDirection(" in block,
      "drawing asks gridViewPlaneForDirection for the view plane")
check("for (const auto& axis : grid.axes)" not in block,
      "drawing must NOT iterate grid.axes (that is the snap/zoom base geometry)")
check("gridViewPlaneForDirection(viewDirection)" in block,
      "the plane class comes from the camera view direction")
# Gorunus yonu KAMERADAN turetilir (kamera API'sine yeni metot eklemeden,
# R ortonormal -> 3. satir = dunya bakis yonu).
check(block.count("camera.viewTransform(") == 3,
      "view direction derived from camera.viewTransform on the three world axes")
check(re.search(r"const GridViewPlane gridPlane\s*=", block) is not None,
      "view plane is computed once per frame (not per grid)")
# Render-only: cizim blogu hicbir durum degistirmemeli (secilemez/snap'lenemez).
check(re.search(r"draft\.[A-Za-z_]+\s*=", block) is None, "grid block never mutates view state")
check(re.search(r"grid\.axes\s*=", block) is None, "grid block never rewrites the derived geometry")
for token in ("selectedModels", "gumball", "snapType", "updateHover", "moveModels"):
    check(token not in block, f"render-only: '{token}' absent from the grid block")

check("enum class GridViewPlane" in header, "GridViewPlane is declared in grid.hpp")
check("GridViewPlane gridViewPlaneForDirection(const Vec3&" in header,
      "gridViewPlaneForDirection is declared in grid.hpp")
check("std::vector<GridAxisLine> gridAxesForView(const GridDefinition& grid, GridViewPlane plane)"
      in header,
      "gridAxesForView is declared in grid.hpp")
# TEK KAYNAK: taban turev (snap/zoom) plan ailelerinden uretilir.
check("gridAxesForView(grid, GridViewPlane::XY)" in grid_source,
      "rebuildGridAxes derives the base geometry through gridAxesForView(XY)")
check(grid_source.count("grid.axes = std::move(axes);") == 1,
      "grid.axes is assigned exactly once (single derived source)")
# Uc aile de tek fonksiyonda: paralel aile atlanmasi acikca kodlanmis.
check("if (plane == GridViewPlane::XY)" in grid_source and
      "if (plane == GridViewPlane::XZ)" in grid_source,
      "gridAxesForView has one branch per view plane (parallel family skipped)")

# --- 2) Davranis: uretim blogu, gercek cekirdek, sahte GDI --------------------
line_match = re.search(r"^void line\(.*?^}", source, re.M | re.S)
assert line_match, "Renderer line() helper not found"
template = (root / "tests/grid_view_plane_cases.cpp").read_text(encoding="utf-8")
for hook, value in [("// @LINE_FUNCTION@", line_match.group()), ("// @GRID_BLOCK@", block)]:
    assert template.count(hook) == 1, f"Missing/duplicate template marker: {hook}"
    template = template.replace(hook, value)
build.mkdir(parents=True, exist_ok=True)
generated = build / "grid_view_plane_generated.cpp"
binary = build / "grid_view_plane_test"
generated.write_text(template, encoding="utf-8")
subprocess.run(["g++", "-std=c++20", "-O0", "-g", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "include"), str(generated), str(root / "src/grid.cpp"),
                str(root / "src/geometry.cpp"), "-o", str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True)
for line in result.stdout.splitlines():
    print(line)
if result.returncode != 0:
    failures.append("behaviour suite failed")

print(f"\ngrid view plane: {'OK' if not failures else 'FAILED'} "
      f"({len(failures)} problem)")
sys.exit(1 if failures else 0)
