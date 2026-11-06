#!/usr/bin/env python3
"""Mutasyon kosucusu: run_snap_marker_scale.py "olcekli marker" kuralini
GERCEKTEN yakaliyor mu?

UI-6a: marker olcegi artık derleme-zamani sabit degil; cizim fonksiyonu
`int scale` parametresi alir ve SnapSettings.markerPenWidth kalem kalinligini
besler. Her mutasyon src/renderer.cpp'yi bozar, testin DUSMESINI bekler, sonra
dosyayi BAYT-BIREBIR geri yazar. Sonda disk == orijinal dogrulanir.

Kullanim: python3 tests/mutate_snap_marker_scale.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"

TARGET = root / "src/renderer.cpp"
ORIG = TARGET.read_text(encoding="utf-8")

CALL = "drawSnapMarkerShape(dc, p, snapMarkerSymbol(draft.snapType), draft.snapMarkerScale);"
# Motion overlay cagri yeri 12 bosluk girintili; normal yol 8. Bu yuzden bu
# baglam TEK ve istenen dal bozulabiliyor.
OVERLAY_CALL = "            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));\n" \
    + "            " + CALL

MUTATIONS = [
    ("M1 olcekleme etkisiz (sm = kimlik)",
     "const auto sm = [scale](int v) { return v * scale; };",
     "const auto sm = [scale](int v) { return v; };"),
    ("M2 bir cizim yolu artik yardimciyi cagirmiyor", OVERLAY_CALL,
     "            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));\n"
     "            switch (snapMarkerSymbol(draft.snapType)) { default: break; }"),
    ("M3 etiket ofseti olceksiz kaldi",
     "drawText(dc, p.x + 10 * draft.snapMarkerScale, p.y + 8 * draft.snapMarkerScale,\n"
     "                     snapTypeLabel(draft.snapType), RGB(90, 255, 145));",
     "drawText(dc, p.x + 10, p.y + 8,\n"
     "                     snapTypeLabel(draft.snapType), RGB(90, 255, 145));"),
    ("M4 kalem kalinligi sabitlendi (ayar yok sayildi)",
     "CreatePen(PS_SOLID, draft.snapMarkerPenWidth,",
     "CreatePen(PS_SOLID, 2,"),
]


def run_test():
    return subprocess.run([sys.executable, str(root / "tests/run_snap_marker_scale.py"), str(build)],
                          capture_output=True).returncode


caught = 0
for label, old, new in MUTATIONS:
    if ORIG.count(old) < 1:
        print(f"SETUP HATASI: parca bulunamadi -> {label}")
        TARGET.write_text(ORIG, encoding="utf-8")
        raise SystemExit(1)
    try:
        TARGET.write_text(ORIG.replace(old, new, 1), encoding="utf-8")
        rc = run_test()
    finally:
        TARGET.write_text(ORIG, encoding="utf-8")
    if rc != 0:
        caught += 1
        print(f"  YAKALANDI  {label}")
    else:
        print(f"  KACIRILDI! {label}")

ref = run_test()
disk_ok = TARGET.read_text(encoding="utf-8") == ORIG
print(f"mutasyon: {caught}/{len(MUTATIONS)} yakalandi; "
      f"referans {'GECTI' if ref == 0 else 'DUSTU'}; dosya geri yazildi={disk_ok}")
raise SystemExit(0 if (caught == len(MUTATIONS) and ref == 0 and disk_ok) else 1)
