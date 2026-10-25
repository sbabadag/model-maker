#!/usr/bin/env python3
"""Mutasyon kosucusu: VIEWCUBE testleri gercek hatalari yakaliyor mu?

Iki katman:
  * LOGIC  — src/view_cube.cpp bozulur, cekirdek testi YENIDEN DERLENIR ve
             kosulur; testin DUSMESI beklenir.
  * SHAPE  — kablolama (cizim cagrisi / olu bildirim / yuz kose kumesi) bozulur;
             run_view_cube.py'nin DUSMESI beklenir.

Her mutasyondan sonra dosya BAYT-BIREBIR geri yazilir; sonda disk == orijinal
dogrulanir. Mutasyonsuz halde iki test de GECMELIDIR.

Kullanim: python3 tests/mutate_view_cube.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"

VC = root / "src/view_cube.cpp"
APP = root / "src/application.cpp"
APPH = root / "include/model_maker/application.hpp"
FILES = {VC: VC.read_text(encoding="utf-8"),
         APP: APP.read_text(encoding="utf-8"),
         APPH: APPH.read_text(encoding="utf-8")}

# (etiket, dosya, eski, yeni, tur)
MUTATIONS = [
    # --- LOGIC: cekirdek testi (yeniden derleme + kosum) ---------------------
    ("M1 Top yuzu yine yanlis dortgen (normal ile uyumsuz)", VC,
     "{StandardView::Top,    {4, 5, 6, 7}, {0.0, 0.0,  1.0}}",
     "{StandardView::Top,    {3, 2, 6, 7}, {0.0, 0.0,  1.0}}", "logic"),
    ("M2 kose gorunurlugu yok sayilir (gizli kose secilebilir)", VC,
     "        if (!cube.cornerVisible[i]) continue;", "        if (false) continue;", "logic"),
    ("M3 kose yaricapi dev (yuz tiklamasi koseye kacar)", VC,
     "distance <= static_cast<long>(kViewCubeCornerRadiusPx) * kViewCubeCornerRadiusPx",
     "distance <= static_cast<long>(1000) * static_cast<long>(1000)", "logic"),
    ("M4 kenar gorunurlugu yok sayilir (gizli kenar secilebilir)", VC,
     "        if (!cube.edgeVisible[i]) continue;", "        if (false) continue;", "logic"),
    ("M5 ISO/ev dugmesi artik hedef degil", VC,
     "    if (contains(cube.homeControl, x, y)) {", "    if (false) {", "logic"),
    ("M6 turntable yaw isareti doner (kamera yonu hedefle uyusmaz)", VC,
     "        yaw = std::atan2(-nx, nz);", "        yaw = std::atan2(nx, nz);", "logic"),
    ("M7 yuz hedefi yanlis matrise gider (Ctrl+1..7 ile ayrilir)", VC,
     "    if (target.kind == ViewCubeTargetKind::Face) return Camera::standardViewRotation(target.face);",
     "    if (target.kind == ViewCubeTargetKind::Face) return Camera::turntableRotation(0.0, 0.0);",
     "logic"),
    ("M8 yuz normal->indeks eslemesi kayar (gorunurluk yanlis)", VC,
     "    if (c > 0) return 3; // Top", "    if (c > 0) return 2; // Top", "logic"),
    ("M9 slerp hedefe hic varmaz", VC,
     "    if (t <= 0.0) return from;", "    if (t <= 1.0) return from;", "logic"),
    ("M10 quaternion->matris isareti bozulur (gidis-donus tutmaz)", VC,
     "    return {1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz),       2.0 * (xz + wy),",
     "    return {1.0 - 2.0 * (yy + zz), 2.0 * (xy + wz),       2.0 * (xz + wy),", "logic"),

    # --- SHAPE: kablolama ---------------------------------------------------
    ("S1 cizim cagrisi kaldirilir (kup gorunmez)", APP,
     "    if (mode_ == EditMode::View3D && !viewDef_)\n        viewCubeRenderer_.draw(dc, client, cursorScreen_, camera_);",
     "    if (false)\n        viewCubeRenderer_.draw(dc, client, cursorScreen_, camera_);", "shape"),
    ("S2 olu bildirim geri gelir (kafa karisikligi)", APPH,
     "    void onCanvasPaint();", "    void onCanvasPaint();\n    void onViewCubePaint();", "shape"),
    ("S3 Front yuzu eski (normal ile uyumsuz) kumeye doner", VC,
     "{StandardView::Front,  {3, 2, 6, 7}, {0.0,  1.0, 0.0}}",
     "{StandardView::Front,  {4, 7, 6, 5}, {0.0,  1.0, 0.0}}", "shape"),
]


def run_logic_test() -> int:
    rc = subprocess.run(["cmake", "--build", str(build), "--target",
                         "model_maker_view_cube_targets_test"],
                        capture_output=True).returncode
    if rc != 0:
        return rc  # derlenmiyorsa da "yakalandi" sayilir
    binary = build / "model_maker_view_cube_targets_test"
    return subprocess.run([str(binary)], capture_output=True).returncode


def run_shape_test() -> int:
    return subprocess.run([sys.executable, str(root / "tests/run_view_cube.py")],
                          capture_output=True).returncode


def restore() -> None:
    for path, text in FILES.items():
        path.write_text(text, encoding="utf-8")


# Referans: mutasyonsuz halde iki test de GECMELI.
if run_logic_test() != 0 or run_shape_test() != 0:
    print("SETUP HATASI: referans (mutasyonsuz) halde testler gecmiyor")
    restore()
    raise SystemExit(1)

caught = 0
for label, path, old, new, kind in MUTATIONS:
    original = FILES[path]
    if original.count(old) != 1:
        print(f"SETUP HATASI: parca bulunamadi/coklu -> {label}")
        restore()
        raise SystemExit(1)
    try:
        path.write_text(original.replace(old, new, 1), encoding="utf-8")
        rc = run_logic_test() if kind == "logic" else run_shape_test()
    finally:
        restore()
    if rc != 0:
        caught += 1
        print(f"  YAKALANDI  [{kind}] {label}")
    else:
        print(f"  KACTI      [{kind}] {label}")

# Dosyalar bayt-birebir geri yazildi mi?
for path, text in FILES.items():
    if path.read_text(encoding="utf-8") != text:
        print(f"HATA: {path} orijinal haline donmedi")
        raise SystemExit(1)

if run_logic_test() != 0 or run_shape_test() != 0:
    print("HATA: geri yazim sonrasi testler gecmiyor")
    raise SystemExit(1)

print(f"\n{caught}/{len(MUTATIONS)} mutasyon yakalandi")
raise SystemExit(0 if caught == len(MUTATIONS) else 1)
