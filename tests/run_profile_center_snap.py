#!/usr/bin/env python3
"""Sanal PROFIL MERKEZI snap noktasi — kaynak sekli + UI baglantisi denetimi.

KULLANICI SARTI: "profilin tam merkezinde bir sanal snap noktasi olustur".

Davranis katmani `tests/profile_center_snap_test.cpp` icinde (gercek
SnapEngine::snap/snap3D). Buradaki katman, C++ testinin GOREMEDIGI baglantiyi
denetler: tur tablolari (etiket/marker/renk), tek-kaynak maske boyutu, Win32
OSNAP listesi + onay kutusu dizisi kilidi ve Qt kurdele menusu.

Native Windows/Qt GUI, fare ya da cizim testi DEGILDIR.
Kullanim: python3 tests/run_profile_center_snap.py
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
header = (root / "include/model_maker/drafting.hpp").read_text(encoding="utf-8")
source = (root / "src/drafting.cpp").read_text(encoding="utf-8")
app_source = (root / "src/application.cpp").read_text(encoding="utf-8")
app_header = (root / "include/model_maker/application.hpp").read_text(encoding="utf-8")
qt_source = (root / "src/qt_main_window.cpp").read_text(encoding="utf-8")

failures = []


def check(ok, label):
    print(("PASS " if ok else "FAIL ") + label)
    if not ok:
        failures.append(label)


# --- 1) Cekirdek: tur + ADI GECEN ILK KAYNAK (maske boyutu) -------------------
check("ProfileCenter" in header, "SnapType::ProfileCenter cekirdekte tanimli")
enum_text = header[header.index("enum class SnapType {"):header.index("};", header.index("enum class SnapType {"))]
check(enum_text.rstrip().endswith("ProfileCenter"),
      "enum SONUNA eklendi (mevcut turlerin indeksleri kaymadi)")
check("ProfileCenter" in header[header.index("enum class SnapType {"):header.index("enum class SnapMarkerSymbol")],
      "enum tanimi ile isaret sembolu tanimi karismiyor")
check("using SnapTypeMask = std::array<bool, static_cast<std::size_t>(SnapType::ProfileCenter) + 1>;" in header,
      "maske boyutu EN SON turden turetilir (tek kaynak, ayri sayi yok)")
check("SnapType::Parallel) + 1>" not in header,
      "eski 'Parallel + 1' boyutu KALMADI (yeni tur disarida kalirdi)")

# --- 2) Aday uretimi: yalniz profil elemanlari, ONBELLEKLI AABB --------------
marker = "        // SANAL PROFIL MERKEZI (kullanici sarti:"
end_marker = "\n    constexpr std::size_t intersectionSegmentBudget"
start = source.index(marker)
block = source[start:source.index(end_marker, start)]
check("SnapType::ProfileCenter" in block, "aday SnapType::ProfileCenter olarak uretilir")
check("profileName.empty()" in block, "yalniz PROFILE elemanlari icin (duz cizgi/kutu etkilenmez)")
check("document.modelBounds()" in source,
      "sinir kutulari Document::modelBounds() (onbellekli AABB) kaynagindan")
check("bounds.minimum" in block and "bounds.maximum" in block,
      "merkez = (minimum + maximum) / 2")
check("std::min(" not in block and "std::max(" not in block,
      "IKINCI bir sinir kutusu hesabi YOK (tek kaynak)")
check("visibleEdges" not in block,
      "kenar gorunurluk filtresinden BAGIMSIZ (sanal nokta; Wireframe/Solid ayni)")

# --- 3) Tur tablolari: etiket / marker / renk --------------------------------
check("case SnapType::ProfileCenter: return 6;" in source,
      "oncelik tablosu tanimli (merkez ailesi)")
check('case SnapType::ProfileCenter: return L"PROF CENTER";' in source,
      "etiket: PROF CENTER")
check("case SnapType::ProfileCenter: return SnapMarkerSymbol::CircleCross;" in source,
      "marker sembolu tanimli")
check("case SnapType::ProfileCenter: return 0x00FF40FF;" in source,
      "marker rengi tanimli ve GeometricCenter'dan FARKLI")
check(source.count("SnapType::ProfileCenter") >= 5,
      "tur; aday + oncelik + etiket + marker + renk tablolarinda bagli")

# --- 4) Win32 OSNAP listesi + onay kutusu dizisi kilidi ----------------------
check('{SnapType::ProfileCenter, L"Profile Center"}' in app_source,
      "Win32 OSNAP listesi (snapChoices) turu icerir")
check("static_assert(snapChoices.size() == Application::kSnapChoiceCount" in app_source,
      "liste ile onay kutusu dizisi DERLEME ZAMANINDA kilitli (static_assert)")
check("static constexpr std::size_t kSnapChoiceCount = 15;" in app_header,
      "onay kutusu dizisi boyutu tek sabitten gelir (15)")
check("std::array<HWND, kSnapChoiceCount> snapTypeCheckboxes_{};" in app_header,
      "dizi sabiti kullanir (ciplak sayi kalmadi)")

# --- 5) Qt kurdele menusu ----------------------------------------------------
check('addSnapItem("⊙ Profile Center",  SnapType::ProfileCenter);' in qt_source,
      "Qt kurdele snap menusunde madde var (ac/kapa)")
check(qt_source.count("SnapType::ProfileCenter") == 1,
      "Qt tarafinda tam olarak BIR kez bagli")

print()
if failures:
    print(f"PROFIL MERKEZI (kaynak sekli): {len(failures)} HATA")
    raise SystemExit(1)
print("PROFIL MERKEZI (kaynak sekli): TAMAM")
raise SystemExit(0)
