#!/usr/bin/env python3
"""Mutasyon kosucusu: model_maker_snap_depth_pick_test GERCEKTEN yakaliyor mu?

Kullanici sartlari (plan gorunusu):
  A) "mid point snap bazen calismiyor"  -> Solid stilde orta nokta snap'i
     kayboluyordu (varsayilan stil Solid; yuzu olmayan cizgi/polyline ve
     normal yonu ic/dis OCC katilari).
  B) plan gorunusunde midpoint YANLIS kotuna snap'liyordu (gorunur ust kenar
     yerine ALTINDAKI gizli kenar; esit ekran mesafesinde siralama bagimli).

Her mutasyon src/drafting.cpp'yi bozar, testi yeniden derler, testin
DUSMESINI bekler, sonra dosyayi BAYT-BIREBIR geri yazar.

Kullanim: python3 tests/mutate_snap_depth_pick.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"

TARGET = "model_maker_snap_depth_pick_test"
REL = "src/drafting.cpp"

GUARD_NEW = "            if (!visibleEdges.empty()) {\n"
GUARD_OLD = ("            if (!visibleEdges.empty() || "
             "(screenSpace && screenSpace->visibleEdgesOnly)) {\n")

FLIP = (
    "                    if (nx * (faceCenter.x - modelCenter.x) +\n"
    "                        ny * (faceCenter.y - modelCenter.y) +\n"
    "                        nz * (faceCenter.z - modelCenter.z) < 0.0) {\n"
    "                        nx = -nx; ny = -ny; nz = -nz;\n"
    "                    }\n")

TIE = "        return viewDepth(a.point) > viewDepth(b.point);\n"
DEPTHCAM = ("        return depthCamera ? depthCamera->viewTransform(point).z\n"
            "                           : 0.0;\n")

# (etiket, eski, yeni)
MUTATIONS = [
    ("M1 derinlik tercihi kaldirildi (gizli kenar kazanir)", TIE,
     "        return false;\n"),
    ("M2 derinlik yonu ters (UZAK aday kazanir)", TIE,
     "        return viewDepth(a.point) < viewDepth(b.point);\n"),
    ("M3 kamera verilmedi (derinlik notr)", DEPTHCAM,
     "        return 0.0;\n"),
    ("M4 filtre korumasi kaldirildi (yuzsuz modelde tum kenarlar elenir)",
     GUARD_NEW, GUARD_OLD),
    ("M5 normal disa yonlendirme kaldirildi (OCC ters normali elenir)",
     FLIP, ""),
]

orig = (root / REL).read_bytes()
text0 = orig.decode("utf-8")


def build_all():
    return subprocess.run(["cmake", "--build", str(build), "-j", "4"],
                          capture_output=True, text=True)


def run_target():
    return subprocess.run([str(build / TARGET)], capture_output=True, text=True)


def restore():
    (root / REL).write_bytes(orig)
    build_all()


results = []
try:
    for label, old, new in MUTATIONS:
        assert text0.count(old) == 1, f"desen tekil degil/bulunamadi: {label}\n{old!r}"
        (root / REL).write_text(text0.replace(old, new, 1), encoding="utf-8")
        log = build_all()
        compiled = log.returncode == 0
        ran = run_target() if compiled else None
        caught = bool(compiled and ran is not None and ran.returncode != 0)
        detail = "" if compiled else "DERLENMEDI (mutasyon kusurlu)"
        if compiled and ran is not None:
            detail = "; ".join(l for l in ran.stdout.splitlines() if l.startswith("FAIL"))[:120]
        results.append((label, caught, detail))
        (root / REL).write_bytes(orig)
        build_all()
        assert (root / REL).read_bytes() == orig, f"geri yazma bozuk: {REL}"
finally:
    (root / REL).write_bytes(orig)
    build_all()

print("\n== MUTASYON SONUCLARI ==")
ok = True
for label, caught, detail in results:
    print(f"{'YAKALANDI' if caught else 'KACIRILDI !!'}  {label}  {detail}")
    ok = ok and caught

ref = run_target()
print(f"referans {TARGET}: exit={ref.returncode}")
ok = ok and ref.returncode == 0

assert (root / REL).read_bytes() == orig, "temizlik bozuk"
print("temizlik: dosya orijinal baytlarla ayni ✓")
print("SONUC:", "TUM MUTASYONLAR YAKALANDI" if ok else "BOSLUK VAR")
sys.exit(0 if ok else 1)
