#!/usr/bin/env python3
"""Kesit YERLESIMI testlerinin (UST YUZEY uye ekseninde) GERCEKTEN
yakaladigini mutasyonla kanitla.

Kullanici sarti (v4): "ust Flansin USTUNDEN cizmiyor ust flansin MERKEZINDEN
ciziyor" → kesit ORTALANMAZ, UST YUZEY uye eksenine oturur ve tum kesit
cizginin ALTINA sarkar (Tekla "position: top of section"). v3'te ust
flansin/plakanin ORTA-KALINLIK duzlemi oturuyordu.

Her mutasyon: TEK degisiklik → `tests/occ_smoke.cpp` icindeki
"OCC SECTION-PLACEMENT" kontrolleri DUSMELI → kaynak AYNEN geri yazilir
(skill uyarisi: `replace("", orig)` YAZMA; orijinali bellekte tut).

Bu betik GERCEK OpenCASCADE gerektirir (occ_smoke yalnizca OCC varsa derlenir).
Linux host: `python3 tests/mutate_section_placement.py`
OCC baska yerde ise: `python3 tests/mutate_section_placement.py <occt-include-dir>`
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
occ_include = sys.argv[1] if len(sys.argv) > 1 else "/usr/include/opencascade"

FILES = {
    "src/occ_geometry.cpp": root / "src/occ_geometry.cpp",
    "src/profile_database.cpp": root / "src/profile_database.cpp",
}
ORIG = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}

# (etiket, dosya, eski, yeni)
MUTATIONS = [
    ("M1 ortalanmis kesit (eski davranis)", "src/occ_geometry.cpp",
     "offsetTrsf.SetTranslation(gp_Vec(0.0, -sectionTopY, 0.0));",
     "offsetTrsf.SetTranslation(gp_Vec(0.0, 0.0, 0.0));"),
    ("M2 kaydirma ters yonde (govde yukari)", "src/occ_geometry.cpp",
     "offsetTrsf.SetTranslation(gp_Vec(0.0, -sectionTopY, 0.0));",
     "offsetTrsf.SetTranslation(gp_Vec(0.0, sectionTopY, 0.0));"),
    ("M3 kaydirma yarim (yarim kalinlik)", "src/occ_geometry.cpp",
     "const double sectionTopY = profileSectionTopY(profile);",
     "const double sectionTopY = profileSectionTopY(profile) / 2.0;"),
    # Ust yuz yerine ust flansin ORTASI (v3 davranisi) — kullanicinin sikayet
    # ettigi tam olarak bu; test bunu yakalamali.
    ("M4 v3: ust flans ORTASI (h/2 - tf/2)", "src/profile_database.cpp",
     "    case ProfileSectionKind::Box:\n"
     "    case ProfileSectionKind::ISection:\n"
     "    default:\n"
     "        // KULLANICI SARTI (v4): kesit uye eksenine UST YUZEYINDEN oturur →\n"
     "        // eksen cizgisi kesitin en ustunden gecer, tum kesit ALTINA sarkar.\n"
     "        // (v3: ust flansin/plakanin ORTASI = h/2 - tf/2 oturuyordu.)\n"
     "        return h / 2.0;",
     "    case ProfileSectionKind::Box:\n"
     "    case ProfileSectionKind::ISection:\n"
     "    default:\n"
     "        return h / 2.0 - (profile.flangeThickness > 0.0 ? profile.flangeThickness\n"
     "                                                       : profile.plateThickness) / 2.0;"),
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


def restore():
    for name, path in FILES.items():
        path.write_text(ORIG[name], encoding="utf-8")


failures = []
try:
    rc, out = build_and_run()
    if rc is None:
        print("OCC derlemesi basarisiz — betik dogrulanamaz:\n" + out)
        sys.exit(1)
    print(f"REFERANS (mutasyonsuz): exit={rc} placement_ok={'SECTION-PLACEMENT OK' in out}")
    if rc != 0 or "SECTION-PLACEMENT OK" not in out:
        failures.append("referans")
    for label, rel, old, new in MUTATIONS:
        assert ORIG[rel].count(old) == 1, f"hedef metin bulunamadi ({rel}): {old!r}"
        text = ORIG[rel].replace(old, new)
        assert text != ORIG[rel], "mutasyon metni degistirmedi"
        FILES[rel].write_text(text, encoding="utf-8")
        try:
            rc, out = build_and_run()
        finally:
            restore()
        caught = rc is None or rc != 0 or "SECTION-PLACEMENT OK" not in out
        print(f"{label}: exit={rc} YAKALANDI={caught}")
        if not caught:
            failures.append(label)
finally:
    restore()

same = all(FILES[name].read_bytes() == ORIG[name].encode("utf-8") for name in FILES)
print(f"kaynak dosyalar bayt-birebir geri yazildi: {same}")
if not same or failures:
    print("MUTASYON TESTI BASARISIZ:", failures)
    sys.exit(1)
print(f"MUTASYON TESTI OK — {len(MUTATIONS)} bozmanin hepsi yakalandi, referans yesil.")
