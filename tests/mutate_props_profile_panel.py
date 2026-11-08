#!/usr/bin/env python3
"""Mutasyon kosucusu: run_props_profile_panel.py gercekten yakaliyor mu?

Her mutasyon src/qt_main_window.cpp ya da src/properties_panel.cpp'yi bozar,
testin DUSMESINI bekler, sonra dosyayi BAYT-BIREBIR geri yazar. Sonda disk ==
orijinal dogrulanir (icerik bellekte tutulur).

Kullanim: python3 tests/mutate_props_profile_panel.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
FILES = {
    "qt_main_window.cpp": root / "src/qt_main_window.cpp",
    "properties_panel.cpp": root / "src/properties_panel.cpp",
}
ORIG = {name: path.read_text(encoding="utf-8") for name, path in FILES.items()}

MUTATIONS = [
    ("M1 katalog bir kez kurulmuyor (setProfileCatalog silindi)", "qt_main_window.cpp",
     'propsPanel_->setProfileCatalog(profileCatalog);',
     '/* katalog kurulmadi */;'),
    ("M2 aktif profil (pending) ayarlanmiyor", "qt_main_window.cpp",
     'app_.setPendingProfileName(name);  // cizilecek kirisin profili',
     '// aktif profil ayarlanmiyor'),
    ("M3 ENTER (returnPressed) baglantisi kaldirildi", "properties_panel.cpp",
     'QObject::connect(profileEdit_, &QLineEdit::returnPressed, this, [this]() {',
     '/* ENTER baglantisi yok */; if (false) [this]() {'),
    ("M4 refresh katalogu yeniden kuruyor (kullanici degeri kaybolur)", "qt_main_window.cpp",
     'propsPanel_->validateProfile =',
     'propsPanel_->setProfileCatalog({});\n    propsPanel_->validateProfile ='),
    ("M5 profileSelector_ panele baglanmiyor", "qt_main_window.cpp",
     'profileSelector_ = propsPanel_->profileEdit();',
     'profileSelector_ = nullptr;'),
    ("M6 canonicalName harf duyarsizligi bozuldu", "qt_main_window.cpp",
     'if (QString::compare(item, text, Qt::CaseInsensitive) == 0)',
     'if (QString::compare(item, text, Qt::CaseSensitive) == 0)'),
    ("M7 katalog disi profil reddedilmiyor", "qt_main_window.cpp",
     'propsPanel_->setProfileValid(false);',
     'propsPanel_->setProfileValid(true);'),
    ("M8 odak tuvale donmuyor (kisayollar calismaz)", "qt_main_window.cpp",
     'if (HWND canvas = app_.activeCanvasHandle()) SetFocus(canvas);',
     '/* odak geri verilmiyor */;'),
    ("M9 validasyon callback'i kurulmadi", "qt_main_window.cpp",
     'propsPanel_->validateProfile =\n        [profileCatalog, canonicalName](const QString& text) {',
     '/* kaldirildi */\n        [profileCatalog, canonicalName](const QString& text) {'),
]


def restore():
    for name, path in FILES.items():
        path.write_text(ORIG[name], encoding="utf-8")


def run_test():
    return subprocess.run([sys.executable,
                           str(root / "tests/run_props_profile_panel.py"),
                           str(build)], capture_output=True).returncode


caught = 0
for label, fname, old, new in MUTATIONS:
    if ORIG[fname].count(old) < 1:
        print(f"SETUP HATASI: parca bulunamadi -> {label} (adet={ORIG[fname].count(old)})")
        restore()
        raise SystemExit(1)
    try:
        # M8: iki yol da kapatilmali (applyProfileNow + UYGULA); digerlerinde
        # tek occurrence yeterli.
        FILES[fname].write_text(ORIG[fname].replace(old, new),
                                encoding="utf-8")
        rc = run_test()
    finally:
        restore()
    if rc != 0:
        caught += 1
        print(f"  YAKALANDI  {label}")
    else:
        print(f"  KACIRILDI! {label}")

# Referans: mutasyonsuz halde test GECMELI
ref = run_test()

disk_ok = all(FILES[name].read_text(encoding="utf-8") == ORIG[name] for name in FILES)
print(f"mutasyon: {caught}/{len(MUTATIONS)} yakalandi; "
      f"referans {'GECTI' if ref == 0 else 'DUSTU'}; dosyalar geri yazildi={disk_ok}")
raise SystemExit(0 if (caught == len(MUTATIONS) and ref == 0 and disk_ok) else 1)
