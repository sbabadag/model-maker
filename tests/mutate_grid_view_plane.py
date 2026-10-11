#!/usr/bin/env python3
"""Mutasyon kosucusu: run_grid_view_plane.py gorunus-duzlemi kuralini
GERCEKTEN yakaliyor mu?

Kullanici sarti: "grid de pencerenin duzlemine gore sadece xy yz xz gridleri
gozukur 3d grid gozukmez" + "grid sadece XY veya YZ veya XZ ye yakinligiyla
BIR KEZ tek duzlemde cizilir, goruntunun 3d degismesi grid in cizilimini
degistirmez". Her mutasyon kurali bozar; testin DUSMESI beklenir. Dosyalar her
mutasyondan sonra BAYT-BIREBIR geri yazilir ve sonda disk == orijinal
dogrulanir (`replace("", orig)` tuzagina dusmemek icin icerik bellekte tutulur).

Kullanim: python3 tests/mutate_grid_view_plane.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"

FILES = {
    "renderer.cpp": root / "src/renderer.cpp",
    "grid.cpp": root / "src/grid.cpp",
    "camera.cpp": root / "src/camera.cpp",
}
ORIG = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}

LOCK_LINE = "lockGridViewDirection();"
PLANE_LINE = ("        const GridViewPlane gridPlane = "
              "gridViewPlaneForDirection(camera.gridViewDirection());")
LIVE_DIRECTION = (
    "        const Vec3 liveDirection{camera.viewTransform(Vec3{1.0, 0.0, 0.0}).z,\n"
    "                                 camera.viewTransform(Vec3{0.0, 1.0, 0.0}).z,\n"
    "                                 camera.viewTransform(Vec3{0.0, 0.0, 1.0}).z};\n"
    "        const GridViewPlane gridPlane = gridViewPlaneForDirection(liveDirection);")
LOCK_ROW = "gridViewDirection_ = Vec3{R_[6], R_[7], R_[8]};"

# (etiket, dosya, eski, yeni)
MUTATIONS = [
    ("M1 cizim taban turevi (grid.axes) kullanir — '3B grid' geri gelir",
     "renderer.cpp",
     "            const std::vector<GridAxisLine> viewAxes = gridAxesForView(grid, gridPlane);",
     "            const std::vector<GridAxisLine>& viewAxes = grid.axes;"),
    ("M2 gorunus duzlemi kameradan DEGIL, sabit XY",
     "renderer.cpp",
     PLANE_LINE,
     "        const GridViewPlane gridPlane = GridViewPlane::XY;"),
    ("M3 gridViewPlaneForDirection her zaman XY doner",
     "grid.cpp",
     "    if (az >= ax - kTieEpsilon && az >= ay - kTieEpsilon) return GridViewPlane::XY;\n"
     "    if (ay >= ax - kTieEpsilon) return GridViewPlane::XZ;\n"
     "    return GridViewPlane::YZ;",
     "    (void)ax; (void)ay; (void)az;\n    return GridViewPlane::XY;"),
    ("M4 XZ gorunusunde PARALEL aile de cizilir",
     "grid.cpp",
     "    if (plane == GridViewPlane::XZ) {",
     "    if (plane == GridViewPlane::XZ) {\n        appendPlanFamilies(grid, e, out);"),
    ("M5 XZ kat cizgisi yanlis eksende uzar",
     "grid.cpp",
     "            line.to = Vec3{o.x + e.spanX + e.right, o.y, z};",
     "            line.to = Vec3{o.x, o.y + e.spanY + e.above, z};"),
    ("M6 gorunus duzlemi CANLI rotasyondan turetilir (kilit yok)",
     "renderer.cpp",
     PLANE_LINE,
     LIVE_DIRECTION),
    ("M7 gorunmez grid de cizilir",
     "renderer.cpp",
     "            if (!grid.visible) continue;",
     "            if (false) continue;"),
    ("M8 kilit reset()'te kurulmaz (varsayilan kilide guvenilir)",
     "camera.cpp",
     "    // Grid gorunus duzlemi kilidi: ISO bakis yonu (Z baskin) -> plan gridi.\n"
     "    " + LOCK_LINE + "\n",
     ""),
    ("M9 kilit yanlis matris satirindan (kamera X ekseni) turetilir",
     "camera.cpp",
     LOCK_ROW,
     "gridViewDirection_ = Vec3{R_[0], R_[1], R_[2]};"),
]


def restore():
    for name, path in FILES.items():
        path.write_text(ORIG[name], encoding="utf-8")


def run_test():
    return subprocess.run([sys.executable,
                           str(root / "tests/run_grid_view_plane.py"),
                           str(build)], capture_output=True).returncode


caught = 0
for label, fname, old, new in MUTATIONS:
    if ORIG[fname].count(old) != 1:
        print(f"SETUP HATASI: parca bulunamadi/coklu -> {label}")
        restore()
        sys.exit(2)
    try:
        FILES[fname].write_text(ORIG[fname].replace(old, new), encoding="utf-8")
        if run_test() != 0:
            print(f"YAKALANDI: {label}")
            caught += 1
        else:
            print(f"KACDI: {label}")
    finally:
        restore()

if run_test() != 0:
    print("MUTASYONSUZ HALDE TEST DUSTU (referans kirli)")
    caught = -999

for name, path in FILES.items():
    on_disk = path.read_text(encoding="utf-8")
    print(f"{'OK' if on_disk == ORIG[name] else 'BOZUK'} geri yazildi: {name}")

print(f"\nMutasyon: {caught}/{len(MUTATIONS)} yakalandi")
sys.exit(0 if caught == len(MUTATIONS) else 1)
