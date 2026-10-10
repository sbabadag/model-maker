#!/usr/bin/env python3
"""Kesit YERLESIMI testlerinin (ust flans/plaka ortasi uye ekseninde) GERCEKTEN
yakaladigini mutasyonla kanitla.

Kullanici sarti (v3): "yine merkezden atiyor profili; secili noktaya UST
FLANSIN ORTASINDAN atilmali" → kesit ORTALANMAZ, ust plakanin orta-kalinlik
duzlemi uye eksenine oturur (Tekla "position: top").

Her mutasyon: `src/occ_geometry.cpp` icinde TEK degisiklik → `tests/occ_smoke.cpp`
icindeki "OCC SECTION-PLACEMENT / EXTRUDE-DIAG" kontrolleri DUSMELI → kaynak AYNEN
geri yazilir (skill uyarisi: `replace("", orig)` YAZMA; orijinali bellekte tut).

Bu betik GERCEK OpenCASCADE gerektirir (occ_smoke yalnizca OCC varsa derlenir).
Linux host: `python3 tests/mutate_section_placement.py`
OCC baska yerde ise: `python3 tests/mutate_section_placement.py <occt-include-dir>`
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
occ_include = sys.argv[1] if len(sys.argv) > 1 else "/usr/include/opencascade"

src = root / "src/occ_geometry.cpp"
ORIG = src.read_text(encoding="utf-8")

MUTATIONS = [
    ("M1 ortalanmis kesit (eski davranis)",
     "offsetTrsf.SetTranslation(gp_Vec(0.0, -topPlateY, 0.0));",
     "offsetTrsf.SetTranslation(gp_Vec(0.0, 0.0, 0.0));"),
    ("M2 kaydirma ters yonde (govde yukari)",
     "offsetTrsf.SetTranslation(gp_Vec(0.0, -topPlateY, 0.0));",
     "offsetTrsf.SetTranslation(gp_Vec(0.0, topPlateY, 0.0));"),
    ("M3 kaydirma yarim (ust yuz yarim kalinlik)",
     "const double topPlateY = profileSectionTopPlateCenterY(profile);",
     "const double topPlateY = profileSectionTopPlateCenterY(profile) / 2.0;"),
]

CMD = ("g++ -std=c++20 -O1 -DMM_HAS_OCC -I include -I {inc} "
       "tests/occ_smoke.cpp src/occ_geometry.cpp src/occ_bridge.cpp "
       "src/geometry.cpp src/profile_database.cpp "
       "-lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo "
       "-lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase "
       "-lTKDESTEP -o /tmp/occ_mut_placement").format(inc=occ_include)


def build_and_run():
    build = subprocess.run(CMD, shell=True, cwd=root, capture_output=True, text=True)
    if build.returncode != 0:
        return None, build.stderr[-800:]
    run = subprocess.run(["/tmp/occ_mut_placement"], capture_output=True, text=True)
    return run.returncode, run.stdout


failures = []
try:
    rc, out = build_and_run()
    if rc is None:
        print("OCC derlemesi basarisiz — betik dogrulanamaz:\n" + out)
        sys.exit(1)
    print(f"REFERANS (mutasyonsuz): exit={rc} placement_ok={'SECTION-PLACEMENT OK' in out}")
    if rc != 0 or "SECTION-PLACEMENT OK" not in out:
        failures.append("referans")
    for label, old, new in MUTATIONS:
        assert ORIG.count(old) == 1, f"hedef metin bulunamadi: {old!r}"
        text = ORIG.replace(old, new)
        assert text != ORIG, "mutasyon metni degistirmedi"
        src.write_text(text, encoding="utf-8")
        try:
            rc, out = build_and_run()
        finally:
            src.write_text(ORIG, encoding="utf-8")
        caught = rc is None or rc != 0 or "SECTION-PLACEMENT OK" not in out
        print(f"{label}: exit={rc} YAKALANDI={caught}")
        if not caught:
            failures.append(label)
finally:
    src.write_text(ORIG, encoding="utf-8")

same = src.read_bytes() == ORIG.encode("utf-8")
print(f"kaynak dosya bayt-birebir geri yazildi: {same}")
if not same or failures:
    print("MUTASYON TESTI BASARISIZ:", failures)
    sys.exit(1)
print(f"MUTASYON TESTI OK — {len(MUTATIONS)} bozmanin hepsi yakalandi, referans yesil.")
