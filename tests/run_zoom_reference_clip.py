#!/usr/bin/env python3
"""Derin zoom'da referans katmani maliyetini dogrular.

Kullanici sarti: "zoom in arttiginda zoom cok yavasliyor bunu hizlandiralim
olduca". Kok neden OLCULDU (tahmin degil): sabit referans katmani (yapi gridi
akslari) DUNYA biriminde tanimlidir ve dogrudan projeksiyonlanir; kirpma
olmadan zoom arttikca AYNI cizgi ekranda dogrusal buyur. Tekerlek animasyonu
~15 ms'de bir kare ister; kare maliyeti cizgi uzunluguyla buyudugu icin derin
zoom'da zoom cok yavaslar ve geri zoom da yavaslar.

Iki katman:
  1. Kaynak sekli: grid aks cizimi ekran-uzayi kirpma kullaniyor mu
     (`clipSegmentToRect`, reference_layer.hpp); UCS glifi EKRAN-OLCEKLI mi
     (`referenceGlyphWorldLength`; eski dunya-birimi ALT SINIR kalmamis mi);
     kalem dongu disinda mi (kare basina GDI nesnesi yarat/yik yok).
  2. Davranis: uretim grid cizim blogu renderer.cpp'den VERBATIM cikarilip
     GERCEK mm::Camera + gercek gridAxesForView ile sahte kaydedici GDI
     sinirinda derlenir. Cizilen segment uzunluklari toplanir; 0..48 detent
     (1.12^48 = 231x) zoom taramasinda toplamin SINIRLI kaldigi, tuvalde
     gorunen hicbir aksin dusurulmedigi ve ham uzunlugun kat kat buyuk oldugu
     olculur. Gorunurluk URETIM KIRPMASINDAN BAGIMSIZ olarak ornekleme ile
     bulunur.

Bu native Windows GUI / GDI rasterizasyon testi DEGILDIR.
Kullanim: python3 tests/run_zoom_reference_clip.py [build-directory]
"""
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
source = (root / "src/renderer.cpp").read_text(encoding="utf-8")
header = (root / "include/model_maker/reference_layer.hpp").read_text(encoding="utf-8")

failures = []


def check(ok, label):
    print(("PASS " if ok else "FAIL ") + label)
    if not ok:
        failures.append(label)


# --- 1) Kaynak sekli ---------------------------------------------------------
check('#include "model_maker/reference_layer.hpp"' in source,
      "renderer includes the reference-layer screen-space helper header")

marker = "        // TEKLA-TARZI YAPI GRIDI:"
start = source.index(marker)
end = source.index("        // CALISMA DUZLEMI HAYALETI (ghost work plane)", start)
block = source[start:end]

check("clipSegmentToRect(" in block,
      "grid axis drawing clips its segments in screen space")
check(re.search(r"const double clipMargin\s*=", block) is not None,
      "clipping rect has an explicit margin (labels near the border are kept)")
check(block.index("clipSegmentToRect(") < block.index("MoveToEx("),
      "clipping happens BEFORE the GDI line is issued")
# Kalem dongu DISINDA: eskiden her aks icin ExtCreatePen + DeleteObject vardi.
axis_loop = block[block.index("for (const auto& axis : viewAxes)"):]
check("ExtCreatePen(" not in axis_loop,
      "the dashed pen is created once per grid, not per axis (no per-frame churn)")
check(block.count("DeleteObject(axisPen);") == 1,
      "the hoisted pen is deleted exactly once")
# Etiket yeri kirpilmamis uctan: zoom'dan bagimsiz sabit.
check("KIRPILMAMIS uctan hesaplanir" in block,
      "label anchor is computed from the UNCLIPPED end (stable under zoom)")

# UCS glifi EKRAN-OLCEKLI olmali: dunya birimi kelepcesi KALMAMALI.
check("referenceGlyphWorldLength(" in source,
      "UCS glyph length comes from referenceGlyphWorldLength (fixed single source)")
check("std::clamp(120.0 / glyphPx" not in source,
      "the old world-unit clamp on the glyph length is gone (it grew with zoom)")
check("referenceGlyphWorldLength(glyphPx, 120.0)" in source,
      "the glyph targets 120 screen pixels")
check("inline double referenceGlyphWorldLength(double pxPerUnit, double targetPixels)" in header,
      "referenceGlyphWorldLength is declared in the portable header")
check("inline bool clipSegmentToRect(double& x0" in header,
      "clipSegmentToRect is declared in the portable header")
check(re.search(r"targetPixels / pxPerUnit", header) is not None,
      "glyph world length = target px / px-per-unit (screen-constant)")

# --- 2) Davranis: uretim blogu + GERCEK kamera + sahte kaydedici GDI ---------
template = (root / "tests/zoom_reference_clip_cases.cpp").read_text(encoding="utf-8")
hook = "// @GRID_BLOCK@"
assert template.count(hook) == 1, f"Missing/duplicate template marker: {hook}"
template = template.replace(hook, block)
build.mkdir(parents=True, exist_ok=True)
generated = build / "zoom_reference_clip_generated.cpp"
binary = build / "zoom_reference_clip_test"
generated.write_text(template, encoding="utf-8")
subprocess.run(["g++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I", str(root / "include"), str(generated),
                str(root / "src/grid.cpp"), str(root / "src/geometry.cpp"),
                str(root / "src/camera.cpp"), "-o", str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True)
for line in result.stdout.splitlines():
    print(line)
if result.returncode != 0:
    failures.append("behaviour suite failed")

print(f"\nzoom reference clip: {'OK' if not failures else 'FAILED'} "
      f"({len(failures)} problem)")
sys.exit(1 if failures else 0)
