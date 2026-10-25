#!/usr/bin/env python3
"""Mutasyon kosucusu: referans-katmani zoom kirpma testleri GERCEKTEN yakaliyor mu?

Kullanici sarti: "zoom in arttiginda zoom cok yavasliyor bunu hizlandiralim
olduca". Duzeltme iki parcali:
  (a) grid akslari ekran uzayinda kirpilir (clipSegmentToRect),
  (b) UCS glifi EKRAN-OLCEKLI olur (referenceGlyphWorldLength; dunya birimi
      kelepcesi kalkar).

Bu betik her parcayi tek tek bozar, `run_zoom_reference_clip.py`'nin
DUSTUGUNU dogrular, sonra dosyalari BAYT-BIREBIR geri yazar. Sonda disk ==
orijinal kontrol edilir (`replace("", orig)` tuzagina dusmemek icin icerik
bellekte tutulur).

Kullanim: python3 tests/mutate_zoom_reference_clip.py [build-dizini]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = sys.argv[1] if len(sys.argv) > 1 else "build-hermes"

FILES = {
    "renderer.cpp": root / "src/renderer.cpp",
}
ORIG = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}

# (etiket, dosya, eski, yeni)
MUTATIONS = [
    ("M1 kirpma KALDIRILDI (ham cizgi dogrudan cizilir)",
     "renderer.cpp",
     "                double x0 = from.x, y0 = from.y, x1 = to.x, y1 = to.y;\n"
     "                if (clipSegmentToRect(x0, y0, x1, y1, clipL, clipT, clipR, clipB) &&\n"
     "                    std::hypot(x1 - x0, y1 - y0) >= 0.5) {",
     "                double x0 = from.x, y0 = from.y, x1 = to.x, y1 = to.y;\n"
     "                if (true) {"),

    ("M2 kirpma dikdortgeni DEV (kirpma pratikte devre disi)",
     "renderer.cpp",
     "            const double clipMargin = 64.0;",
     "            const double clipMargin = 1.0e9;"),

    ("M3 UCS glifi dunya-birimi ALT SINIRINA geri dondu (zoom ile buyur)",
     "renderer.cpp",
     "        const double glyphLen = referenceGlyphWorldLength(glyphPx, 120.0);",
     "        const double glyphLen = std::clamp(120.0 / glyphPx, 60.0, 60000.0);"),

    ("M4 kalem her aks icin yeniden yaratiliyor (kare basina GDI nesnesi)",
     "renderer.cpp",
     "            for (const auto& axis : viewAxes) {\n"
     "                const POINT from = projectPoint(axis.from);",
     "            for (const auto& axis : viewAxes) {\n"
     "                LOGBRUSH churn{BS_SOLID, axisColor, 0};\n"
     "                DWORD churnPattern[] = {5, 3};\n"
     "                HPEN churnPen = ExtCreatePen(PS_GEOMETRIC | PS_USERSTYLE | PS_ENDCAP_FLAT,\n"
     "                                             1, &churn, 2, churnPattern);\n"
     "                DeleteObject(churnPen);\n"
     "                const POINT from = projectPoint(axis.from);"),
]


def run_test():
    result = subprocess.run([sys.executable, str(root / "tests/run_zoom_reference_clip.py"), build],
                            capture_output=True, text=True)
    return result.returncode, result.stdout


def restore():
    for name, path in FILES.items():
        if path.read_text(encoding="utf-8") != ORIG[name]:
            path.write_text(ORIG[name], encoding="utf-8")


baseline_code, baseline_out = run_test()
print(f"baseline (mutasyonsuz): exit={baseline_code}")
if baseline_code != 0:
    print(baseline_out)
    print("REFERANS GECMEDI — mutasyon kosusu anlamsiz.")
    sys.exit(1)

caught = 0
try:
    for label, name, old, new in MUTATIONS:
        path = FILES[name]
        source = ORIG[name]
        if source.count(old) != 1:
            print(f"SKIP {label}: desen bulunamadi (kod degismis olabilir)")
            continue
        path.write_text(source.replace(old, new), encoding="utf-8")
        code, out = run_test()
        restore()
        ok = code != 0
        caught += 1 if ok else 0
        print(("CAUGHT " if ok else "MISSED ") + label)
        if not ok:
            print(out)
finally:
    restore()

for name, path in FILES.items():
    assert path.read_text(encoding="utf-8") == ORIG[name], f"{name} bayt-birebir geri yazilmadi"

print(f"\nzoom reference clip mutations: {caught}/{len(MUTATIONS)} yakalandi; "
      f"dosyalar bayt-birebir geri yazildi")
sys.exit(0 if caught == len(MUTATIONS) else 1)
