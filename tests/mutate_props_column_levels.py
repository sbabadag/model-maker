#!/usr/bin/env python3
"""Mutasyon kosucusu: run_props_column_levels.py kusurlari GERCEKTEN yakaliyor mu?

Kullanici: "props taki ozellikler secili elemana saglikli uygulanmiyor ozellikle
kolon boyunu degistirmiyor hepsini kontrol et". Her mutasyon bir regresyonu
(eski hatayi) taklit eder; test DUSMELI. Sonda dosyalar BAYT-BIREBIR geri
yazilir (icerik bellekte tutulur).

Kullanim: python3 tests/mutate_props_column_levels.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"

FILES = {
    "application.cpp": root / "src/application.cpp",
    "qt_main_window.cpp": root / "src/qt_main_window.cpp",
}
ORIG = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}

MUTATIONS = [
    # M1: ByLayer'da erken return (eski hata: ByLayer secmek hicbir sey yapmiyordu)
    ("M1 ByLayer erken return (eski hata)", "application.cpp",
     "    document_.setModelColor(selectedModels_, choice.color);\n"
     "    updateControls();\n"
     "    invalidateCanvas();\n"
     "}\n\n"
     "void Application::setCurrentLineTypeChoice",
     "    if (!choice.color) return;  // MUTASYON\n"
     "    document_.setModelColor(selectedModels_, *choice.color);\n"
     "    updateControls();\n"
     "    invalidateCanvas();\n"
     "}\n\n"
     "void Application::setCurrentLineTypeChoice"),
    # M2: ByLayer icin sentinel eslesmesi (eski hata: panel rengi yansitmiyordu)
    ("M2 renk indeksi ByLayer sentinel (eski hata)", "application.cpp",
     "    if (!props.trueColor) return 0;\n"
     "    for (std::size_t i = 0; i < colorChoices.size(); ++i)\n"
     "        if (colorChoices[i].color && *colorChoices[i].color == *props.trueColor)",
     "    const auto color = props.trueColor.value_or(0xFFFFFFFFu);  // MUTASYON\n"
     "    for (std::size_t i = 0; i < colorChoices.size(); ++i)\n"
     "        if (colorChoices[i].color && *colorChoices[i].color == color)"),
    # M3: refresh kolon kotunu secimden okumaz (eski hata: hep 8500/0)
    ("M3 refresh secimden kolon kotu okumaz", "qt_main_window.cpp",
     "        if (app_.selectedEntityColumnLevels(selectionTop, selectionBottom))",
     "        if (app_.selectedEntityColumnLevelsX(selectionTop, selectionBottom))"),
    # M4: UYGULA kolon kotlarini uygulamaz (eski hata)
    ("M4 UYGULA kolon kotlarini uygulamaz", "qt_main_window.cpp",
     "            propsPanel_->isColumnDirty() ? &top : nullptr,",
     "            nullptr,  // MUTASYON: kot uygulanmaz"),
    # M5: ust/alt kot ters okunur (kolon boyu yanlis)
    ("M5 ust/alt kot ters okunur", "application.cpp",
     "    bottom = props.axisFromZ < props.axisToZ ? props.axisFromZ : props.axisToZ;\n"
     "    top = props.axisFromZ < props.axisToZ ? props.axisToZ : props.axisFromZ;",
     "    top = props.axisFromZ < props.axisToZ ? props.axisFromZ : props.axisToZ;  // MUTASYON\n"
     "    bottom = props.axisFromZ < props.axisToZ ? props.axisToZ : props.axisFromZ;"),
    # M6: duseysellik kapisi kaldirilir (yatay kiris de kolon sayilir)
    ("M6 duseysellik kapisi etkisiz", "application.cpp",
     "    if (std::abs(dz) < std::sqrt(dx * dx + dy * dy)) return false;",
     "    if (std::abs(dz) < std::sqrt(dx * dx + dy * dy) * 0.0) return false;  // MUTASYON"),
    # M7: applyColumnLevels secime uygulamaz (yalniz bekleyen deger)
    ("M7 applyColumnLevels secime uygulamaz", "application.cpp",
     "    setSelectedEntityColumnLevels(top, bottom);\n"
     "}\n\n"
     "#ifdef MM_HAS_OCC",
     "    // MUTASYON: secime uygulama yok\n"
     "}\n\n"
     "#ifdef MM_HAS_OCC"),
    # M10: no-op korumasi etkisiz (kot ayni olsa da bos geri-alma kaydi acilir)
    ("M10 no-op korumasi etkisiz", "application.cpp",
     "        if (std::abs(currentTop - top) <= eps && std::abs(currentBottom - bottom) <= eps)",
     "        if (false && std::abs(currentTop - top) <= eps && "
     "std::abs(currentBottom - bottom) <= eps)  // MUTASYON"),
    # M8: kolon ust satirinda ENTER yolu (onColumnLevelsEnter) bosaltildi
    ("M8 Kolon Ust satirinda ENTER yok", "qt_main_window.cpp",
     "    propsPanel_->onColumnLevelsEnter = [this]() {\n"
     "        app_.applyColumnLevels(propsPanel_->columnTop(),\n"
     "                               propsPanel_->columnBottom());\n"
     "    };",
     "    propsPanel_->onColumnLevelsEnter = [this]() {\n"
     "        /* MUTASYON: ENTER yolu bos */\n"
     "    };"),
    # M9: undo kaydi acilmaz (Ctrl+Z calismaz)
    ("M9 kolon kotu undo kaydi yok", "application.cpp",
     "    if (targets.empty()) {\n"
     "        publishStatus(L\"Kolon kotlari icin dusey profilli bir eleman secin \"\n"
     "                      L\"(yatay kirise uygulanmaz).\");\n"
     "        return;\n"
     "    }\n\n"
     "    if (openUndo) pushUndoSnapshot();",
     "    if (targets.empty()) {\n"
     "        publishStatus(L\"Kolon kotlari icin dusey profilli bir eleman secin \"\n"
     "                      L\"(yatay kirise uygulanmaz).\");\n"
     "        return;\n"
     "    }\n"
     "    // MUTASYON: undo yok"),
]


def restore():
    for name, path in FILES.items():
        path.write_text(ORIG[name], encoding="utf-8")


def run_test():
    return subprocess.run([sys.executable,
                           str(root / "tests/run_props_column_levels.py"),
                           str(build)], capture_output=True).returncode


caught = 0
for label, fname, old, new in MUTATIONS:
    if ORIG[fname].count(old) != 1:
        print(f"SETUP HATASI: parca bulunamadi/coklu -> {label} "
              f"(adet={ORIG[fname].count(old)})")
        restore()
        raise SystemExit(1)
    try:
        FILES[fname].write_text(ORIG[fname].replace(old, new, 1), encoding="utf-8")
        rc = run_test()
    finally:
        restore()
    if rc != 0:
        caught += 1
        print(f"  YAKALANDI  {label}")
    else:
        print(f"  KACIRILDI! {label}")

ref = run_test()
disk_ok = all(FILES[name].read_text(encoding="utf-8") == ORIG[name] for name in FILES)
print(f"mutasyon: {caught}/{len(MUTATIONS)} yakalandi; "
      f"referans {'GECTI' if ref == 0 else 'DUSTU'}; dosyalar geri yazildi={disk_ok}")
raise SystemExit(0 if (caught == len(MUTATIONS) and ref == 0 and disk_ok) else 1)
