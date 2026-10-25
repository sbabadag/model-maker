#!/usr/bin/env python3
"""Mutasyon kosucusu: ekran donus merkezi testleri gercek regresyonu
yakaliyor mu?

Kullanici sarti: "R tusu bir rotate merkezi belirler ... ctrl+mouse top bas
ile ekrani o merkez etrafinda rotate eder".

Her mutasyon KAYNAK SEKLI testini (tests/run_rotate_center.py) ve COK ADIMLI
cekirdek testini (tests/orbit_pivot_test.cpp) hedefler. M6 ozellikle onemli:
kararsiz W kurulumu (R' = R * R^T * rx * R * ry) derlenir ama ortonormallik
hatasini her adimda ~3-4 KATINA buyutur — yalnizca onlarca adimlik dizi
kosan test yakalar (tek adimlik testler YESIL kalir).

Her mutasyon dosyalari gecici bozar, testlerin DUSMESINI bekler, sonra
BAYT-BIREBIR geri yazar (sonda disk == orijinal dogrulanir).

Kullanim: python3 tests/mutate_rotate_center.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"

FILES = {
    "application.cpp": root / "src/application.cpp",
    "application.hpp": root / "include/model_maker/application.hpp",
    "camera.cpp": root / "src/camera.cpp",
    "renderer.cpp": root / "src/renderer.cpp",
}
ORIG = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}

# Her mutasyon: (etiket, [(dosya, eski, yeni), ...])
MUTATIONS = [
    ("M1 R tusu merkez secmiyor (eski sifirlama)", [
        ("application.cpp", "                startRotateCenterCommand();",
         "                resetView();"),
    ]),
    ("M2 Ctrl+sol tus jesti beklemiyor (Ctrl+tik secimi kirilir)", [
        ("application.cpp", "            ctrlRotatePending_ = true;",
         "            ctrlRotatePending_ = false;"),
    ]),
    ("M3 merkez W yerine W^T ile donuyor (pivot savrulur)", [
        ("camera.cpp",
         "    const Vec3 worldOffset{R_[0] * inPrevious.x + R_[3] * inPrevious.y + R_[6] * inPrevious.z,\n"
         "                           R_[1] * inPrevious.x + R_[4] * inPrevious.y + R_[7] * inPrevious.z,\n"
         "                           R_[2] * inPrevious.x + R_[5] * inPrevious.y + R_[8] * inPrevious.z};",
         "    const Vec3 worldOffset{previous[0] * inPrevious.x + previous[1] * inPrevious.y + previous[2] * inPrevious.z,\n"
         "                           previous[3] * inPrevious.x + previous[4] * inPrevious.y + previous[5] * inPrevious.z,\n"
         "                           previous[6] * inPrevious.x + previous[7] * inPrevious.y + previous[8] * inPrevious.z};"),
    ]),
    ("M4 pivot isareti cizilmiyor (geri bildirim yok)", [
        ("renderer.cpp", "        if (draft.orbitPivotVisible) {", "        if (false) {"),
    ]),
    ("M5 esik devre disi (her Ctrl+tik rotasyona terfi eder)", [
        ("application.hpp", "kCtrlRotateDragThresholdPx = 4;", "kCtrlRotateDragThresholdPx = -1;"),
        ("application.cpp",
         "        if (std::abs(x - ctrlRotateStart_.x) + std::abs(y - ctrlRotateStart_.y) >\n"
         "            kCtrlRotateDragThresholdPx) {",
         "        if (true) {"),
    ]),
    ("M6 KARARSIZ formul: R' = R * R^T * rx * R * ry (R^T*R hatayi buyutur)", [
        ("camera.cpp",
         "    const std::array<double, 9> previous = R_;\n"
         "    R_ = mat3mul(rx, mat3mul(R_, ry));",
         "    const std::array<double, 9> previous = R_;\n"
         "    const std::array<double, 9> rtU{R_[0], R_[3], R_[6], R_[1], R_[4], R_[7], R_[2], R_[5], R_[8]};\n"
         "    R_ = mat3mul(R_, mat3mul(rtU, mat3mul(rx, mat3mul(R_, ry))));"),
    ]),
]


def restore():
    for name, path in FILES.items():
        path.write_text(ORIG[name], encoding="utf-8")


def run_python_test():
    return subprocess.run([sys.executable, str(root / "tests/run_rotate_center.py"),
                           str(build)], capture_output=True).returncode


def run_core_test():
    cmake_cache = build / "CMakeCache.txt"
    if not cmake_cache.exists():
        return None
    subprocess.run(["cmake", "--build", str(build), "--target",
                    "model_maker_orbit_pivot_test"], capture_output=True)
    binary = build / "model_maker_orbit_pivot_test"
    if not binary.exists():
        return None
    return subprocess.run([str(binary)], capture_output=True).returncode


caught = 0
core_available = (build / "CMakeCache.txt").exists()
for label, edits in MUTATIONS:
    setup_ok = True
    for fname, old, new in edits:
        if ORIG[fname].count(old) != 1:
            print(f"SETUP HATASI: parca bulunamadi/coklu -> {label} ({fname})")
            setup_ok = False
            break
    if not setup_ok:
        restore()
        raise SystemExit(1)
    try:
        for fname, old, new in edits:
            FILES[fname].write_text(ORIG[fname].replace(old, new, 1), encoding="utf-8")
        rc_py = run_python_test()
        rc_core = run_core_test()
    finally:
        restore()
    where = []
    if rc_py != 0:
        where.append("sekil/davranis")
    if rc_core is not None and rc_core != 0:
        where.append("cekirdek")
    if where:
        caught += 1
        print(f"  YAKALANDI  {label}  [{', '.join(where)}]")
    else:
        print(f"  KACIRILDI! {label}")

ref_py = run_python_test()
ref_core = run_core_test()
disk_ok = all(FILES[name].read_text(encoding="utf-8") == ORIG[name] for name in FILES)
reference_ok = (ref_py == 0) and (ref_core in (0, None))
print(f"mutasyon: {caught}/{len(MUTATIONS)} yakalandi; "
      f"referans {'GECTI' if reference_ok else 'DUSTU'}; "
      f"dosyalar geri yazildi={disk_ok}"
      + ("" if core_available else "  (cekirdek testi: build dizini yok, atlandi)"))
raise SystemExit(0 if (caught == len(MUTATIONS) and reference_ok and disk_ok) else 1)
