#!/usr/bin/env python3
"""Sanal PROFIL MERKEZI testlerinin GERCEKTEN yakaladigini mutasyonla kanitla.

Kullanici sarti: "profilin tam merkezinde bir sanal snap noktasi olustur".

Her mutasyon, ozelligin bir parcasini bozar ve ILGILI denetleyicinin
DUSTUGUNU dogrular:
  * cpp -> davranis katmani (tests/profile_center_snap_test.cpp, gercek
           SnapEngine'i cagirir): hedef yeniden derlenip kosulur.
  * py  -> kaynak sekli + UI baglantisi (tests/run_profile_center_snap.py):
           hizli, derleme gerektirmez.

Dosyalar sonda BAYT-BIREBIR geri yazilir (skill uyarisi: `replace("", orig)`
YAZMA — bos string her karakter arasina girer ve dosyayi bozar).

Kullanim: python3 tests/mutate_profile_center_snap.py [build-dizini]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = sys.argv[1] if len(sys.argv) > 1 else "build-hermes"

DRAFTING_CPP = "src/drafting.cpp"
DRAFTING_HPP = "include/model_maker/drafting.hpp"
APP_CPP = "src/application.cpp"
APP_HPP = "include/model_maker/application.hpp"
QT_CPP = "src/qt_main_window.cpp"

FILES = {
    DRAFTING_CPP: root / DRAFTING_CPP,
    DRAFTING_HPP: root / DRAFTING_HPP,
    APP_CPP: root / APP_CPP,
    APP_HPP: root / APP_HPP,
    QT_CPP: root / QT_CPP,
}
ORIG = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}

CENTER_LINES = (
    "            const Bounds3& bounds = modelBounds[modelIndex];\n"
    "            addCandidate(candidates, (bounds.minimum + bounds.maximum) * 0.5,\n"
    "                         SnapType::ProfileCenter, tolerance, metric);"
)

# (etiket, denetleyici, dosya, eski, yeni)
MUTATIONS = [
    ("M1 aday uretimi tamamen kapatildi",
     "cpp", DRAFTING_CPP,
     "        if (!model.properties().profileName.empty() && modelIndex < modelBounds.size()) {",
     "        if (false) {"),

    ("M2 merkez AABB yerine VERTEX ORTALAMASI (sanal nokta kayar)",
     "cpp", DRAFTING_CPP,
     CENTER_LINES,
     "            Vec3 mean{};\n"
     "            for (const auto& p : vertices) mean = mean + p;\n"
     "            mean = mean * (1.0 / static_cast<double>(vertices.size()));\n"
     "            addCandidate(candidates, mean, SnapType::ProfileCenter, tolerance, metric);"),

    ("M3 profil kapisi kaldirildi (duz cizgi/kutu da merkez uretir)",
     "cpp", DRAFTING_CPP,
     "        if (!model.properties().profileName.empty() && modelIndex < modelBounds.size()) {",
     "        if (modelIndex < modelBounds.size()) {"),

    ("M4 etiket bosaltildi (PROF CENTER kayboldu)",
     "cpp", DRAFTING_CPP,
     '    case SnapType::ProfileCenter: return L"PROF CENTER";',
     '    case SnapType::ProfileCenter: return L"";'),

    ("M5 renk GeometricCenter ile AYNI (ayirt edilemez)",
     "cpp", DRAFTING_CPP,
     "    case SnapType::ProfileCenter: return 0x00FF40FF;   // parlak magenta (merkez ailesi)",
     "    case SnapType::ProfileCenter: return 0x009C6A14;   // koyu turuncu"),

    ("M6 maske boyutu 'Parallel + 1'e DONDU (yeni tur disarida kalir)",
     "py", DRAFTING_HPP,
     "using SnapTypeMask = std::array<bool, static_cast<std::size_t>(SnapType::ProfileCenter) + 1>;",
     "using SnapTypeMask = std::array<bool, static_cast<std::size_t>(SnapType::Parallel) + 1>;"),

    ("M7 Qt kurdele menusunden madde kaldirildi (kapatilamaz)",
     "py", QT_CPP,
     '        addSnapItem("⊙ Profile Center",  SnapType::ProfileCenter);\n',
     ""),

    ("M8 Win32 OSNAP listesindeki tur etiketi degistirildi",
     "py", APP_CPP,
     '{SnapType::ProfileCenter, L"Profile Center"}',
     '{SnapType::ProfileCenter, L"Profil Merkezi"}'),

    ("M9 onay kutusu dizisi ciplak sayiya dondu (tek kaynak kilidi kirildi)",
     "py", APP_HPP,
     "    std::array<HWND, kSnapChoiceCount> snapTypeCheckboxes_{};",
     "    std::array<HWND, 15> snapTypeCheckboxes_{};"),

    ("M10 IKINCI sinir kutusu hesabi eklendi (tek kaynak bozuldu)",
     "py", DRAFTING_CPP,
     CENTER_LINES,
     "            Vec3 lo = vertices.front(), hi = vertices.front();\n"
     "            for (const auto& p : vertices) {\n"
     "                lo = Vec3{std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};\n"
     "                hi = Vec3{std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};\n"
     "            }\n"
     "            addCandidate(candidates, (lo + hi) * 0.5, SnapType::ProfileCenter, tolerance, metric);"),
]

PY_CMD = [sys.executable, str(root / "tests/run_profile_center_snap.py")]
CPP_BUILD = ["cmake", "--build", build, "--target", "model_maker_profile_center_snap_test"]


def run_cpp():
    """Davranis testi: derle + kos. True=gecti, False=dustu, None=derlenmedi."""
    buildp = subprocess.run(CPP_BUILD, cwd=root, capture_output=True, text=True)
    if buildp.returncode != 0:
        return None, "DERLENMEDI"
    p = subprocess.run([str(root / build / "model_maker_profile_center_snap_test")],
                       cwd=root, capture_output=True, text=True)
    last = (p.stdout or "").strip().splitlines()[-1] if p.stdout else ""
    return p.returncode == 0, last


def run_py():
    p = subprocess.run(PY_CMD, cwd=root, capture_output=True, text=True)
    last = (p.stdout or "").strip().splitlines()[-1] if p.stdout else ""
    return p.returncode == 0, last


def restore():
    for name, path in FILES.items():
        path.write_text(ORIG[name], encoding="utf-8")


failures = 0
try:
    ok_cpp, s_cpp = run_cpp()
    print(f"REFERANS cpp (mutasyonsuz): {'GECTI' if ok_cpp else 'DUSTU'} — {s_cpp}")
    ok_py, s_py = run_py()
    print(f"REFERANS py  (mutasyonsuz): {'GECTI' if ok_py else 'DUSTU'} — {s_py}")
    if ok_cpp is not True or ok_py is not True:
        print("HATA: referans kosular gecmeliydi; mutasyon testi anlamsiz.")
        raise SystemExit(2)

    for label, checker, fname, old, new in MUTATIONS:
        path = FILES[fname]
        src = ORIG[fname]
        if old not in src:
            print(f"SKIP {label}: hedef metin bulunamadi ({fname})")
            failures += 1
            continue
        path.write_text(src.replace(old, new, 1), encoding="utf-8")
        try:
            if checker == "cpp":
                ok, summary = run_cpp()
            else:
                ok, summary = run_py()
        finally:
            restore()
        if ok is None:
            print(f"WARN {label}: mutasyon derlenmedi (davranis testi kosmadi)")
            failures += 1
        elif ok:
            print(f"FAIL {label}: [{checker}] test DUSMEDI (mutasyon yakalanmadi!)")
            failures += 1
        else:
            print(f"OK   {label}: [{checker}] test dustu — {summary}")

    for name, path in FILES.items():
        if path.read_text(encoding="utf-8") != ORIG[name]:
            print(f"FAIL {name}: geri yazma BAYT-BIREBIR degil!")
            failures += 1
    print(f"\n{len(MUTATIONS)} mutasyon, {failures} sorun")
finally:
    restore()

raise SystemExit(1 if failures else 0)
