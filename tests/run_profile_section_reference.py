#!/usr/bin/env python3
"""Profil kesiti referans normali — UYGULAMA BAGLANTISI kaynak-sekli testi.

KULLANICI SARTI: "profil cizilirken profil ust basligi workplane ile paralel
olacak."

Cekirdek davranis tests/section_frame_test.cpp + tests/section_normal_test.cpp
ile olculur (gercek fonksiyonlar). Burada olculen sey UYGULAMANIN cekirdegi
DOGRU sekilde kullanmasidir; Windows'a ozel OCC yolu Linux'ta derlenemedigi
icin kaynak-sekli seklinde dogrulanir (diger run_*.py testleriyle ayni desen).
Gercek Windows/OpenCASCADE davranisini KANITLAMAZ.

Kullanim: python3 tests/run_profile_section_reference.py
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
app = (root / "src/application.cpp").read_text(encoding="utf-8")
occ = (root / "src/occ_geometry.cpp").read_text(encoding="utf-8")
geom_h = (root / "include/model_maker/geometry.hpp").read_text(encoding="utf-8")
occ_h = (root / "include/model_maker/occ_geometry.hpp").read_text(encoding="utf-8")

checks = 0
failures = 0


def check(ok: bool, what: str) -> None:
    global checks, failures
    checks += 1
    print(f"{'PASS' if ok else 'FAIL'} {what}")
    if not ok:
        failures += 1


def body(text: str, signature: str, end: str = "\n}\n") -> str:
    start = text.index(signature)
    stop = text.index(end, start)
    return text[start:stop]


# --- 1) Tek kaynak: Application::profileSectionReference --------------------
ref_body = body(app, "Vec3 Application::profileSectionReference() const")
check("profileSectionReferenceNormal(workPlane_" in ref_body and "viewDef_" in ref_body,
      "profileSectionReference: aktif is duzlemi + gorunus dilimi cekirdege verilir")
check(app.count("Vec3 Application::profileSectionReference() const") == 1,
      "profileSectionReference TEK tanim (kopya mantik yok)")

# Gumball test harness'i (run_gumball_interaction.py) gumballProject..draftView
# arasini cekip SAHTE bir Application'da derler; bu metot o dilimde olursa
# workPlane_/viewDef_ bulunamaz ve test DERLENMEZ.
slice_start = app.index("Vec2 Application::gumballProject")
slice_end = app.index("DraftView Application::draftView")
check(not (slice_start < app.index("Vec3 Application::profileSectionReference() const") < slice_end),
      "profileSectionReference gumball harness diliminin DISINDA (test derlenebilir)")

# --- 2) Kati URETIMI referansi gecirir ve SAKLAR ----------------------------
assign = body(app, "void Application::assignProfileToSelection(")
check("const Vec3 sectionReference = profileSectionReference();" in assign,
      "profil atama: referans normal uretimden ONCE bir kez hesaplanir")
check(re.search(r"extrudeProfileSolid\(\s*\*profile,\s*from,\s*to,\s*axis\.rotation,\s*"
                r"sectionReference\s*\)", assign) is not None,
      "profil atama: referans normal extrudeProfileSolid'e GECIRILIR")
check(all(f"solidProps.sectionNormal{k} = sectionReference.{k.lower()};" in assign
          for k in ("X", "Y", "Z")),
      "profil atama: referans normal KATIYLA SAKLANIR (gumball verisi)")

# --- 3) Yeniden uretim SAKLANAN degeri kullanir -----------------------------
regen = body(app, "void Application::setSelectedEntityProfileRotation")
check("info.sectionNormal[0] = props.sectionNormalX;" in regen,
      "yeniden uretim: referans normal mevcut ozelliklerden KOPYALANIR")
check("solidProps.sectionNormalZ = info.sectionNormal[2];" in regen,
      "yeniden uretim: referans normal geri YAZILIR")
check("info.sectionNormal[0], info.sectionNormal[1], info.sectionNormal[2]" in regen,
      "yeniden uretim: extrude SAKLANAN referansla yapilir (aktif duzlem DEGIL)")

grip = body(app, "void Application::reExtrudeProfileGrip")
check("props.sectionNormalX, props.sectionNormalY," in grip and
      "props.sectionNormalZ" in grip,
      "profil ucu tasima: SAKLANAN referans normal korunur")

# --- 4) Gumball yerel cercevesi saklanan referanstan ------------------------
frame = body(app, "void Application::gumballUpdateFrame()")
check("profileSectionFrame(\n            ex, Vec3{props.sectionNormalX, "
      "props.sectionNormalY, props.sectionNormalZ})" in frame,
      "gumball cercevesi props.sectionNormal kullanir (katiyla birebir)")
check("profileSectionFrame(ex);" not in app and "profileSectionFrame(ex)" not in app,
      "application.cpp'de referanssiz profileSectionFrame cagrisi KALMADI")

# --- 5) extruude cagrilarinin HEPSI referans gecirir ------------------------
calls = re.findall(r"extrudeProfileSolid\([^;]*?\)", app, re.S)
check(len(calls) == 3, f"application.cpp'de 3 extrude cagrisi (bulunan: {len(calls)})")
for i, call in enumerate(calls):
    args = call[call.index("(") + 1:call.rindex(")")]
    n = args.count(",")
    check(n >= 4, f"extrude cagrisi #{i + 1}: referans normal argumani var (virgul={n})")

# --- 6) Cekirdek ve OCC imzalari -------------------------------------------
check("const Vec3& referenceNormal = Vec3{0.0, 0.0, 1.0}" in geom_h,
      "profileSectionFrame: referans normal varsayilani (0,0,1) = eski davranis")
check("const Vec3& referenceNormal = Vec3{0.0, 0.0, 1.0}" in occ_h,
      "extrudeProfileSolid: referans normal varsayilani (0,0,1)")
check("profileSectionFrame(Vec3{direction.X(), direction.Y(), direction.Z()}, referenceNormal)" in occ,
      "OCC katisi: kesit cercevesi referans normalla kurulur")
check("gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0))" in occ,
      "OCC: kesit duzleminde uretilir, sonra hedef cerceveye tasinir (degismedi)")

print(f"{checks} checks, {failures} failures")
sys.exit(0 if failures == 0 else 1)
