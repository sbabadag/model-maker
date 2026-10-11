#!/usr/bin/env python3
"""Mutasyon kosucusu: run_props_profile_panel.py gercekten yakaliyor mu?

Her mutasyon src/qt_main_window.cpp'yi bozar, testin DUSMESINI bekler, sonra
dosyayi BAYT-BIREBIR geri yazar. Sonda disk == orijinal dogrulanir
(`replace("", orig)` tuzagina dusmemek icin icerik bellekte tutulur).

Kullanim: python3 tests/mutate_props_profile_panel.py [build-directory]
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
src = root / "src/qt_main_window.cpp"
ORIG = src.read_text(encoding="utf-8")

MUTATIONS = [
    ("M1 yer tutucu oge verisi kaldirildi",
     'propsProfile->addItem(profileNoneLabel(), QString());',
     'propsProfile->addItem(profileNoneLabel());'),
    ("M2 aktif profil (pending) ayarlanmiyor",
     'app_.setPendingProfileName(name);            // cizilecek kirisin profili',
     '// aktif profil ayarlanmiyor'),
    ("M3 ENTER (returnPressed) baglantisi kaldirildi",
     'QObject::connect(profileEdit, &QLineEdit::returnPressed, this, applyProfileField);',
     '/* ENTER baglantisi yok */;'),
    ("M4 refresh listeyi yeniden kuruyor (clear geri geldi)",
     '        QSignalBlocker profileBlocker(propsProfile);\n        if (!profile.isEmpty()) {',
     '        QSignalBlocker profileBlocker(propsProfile);\n        propsProfile->clear();\n        if (!profile.isEmpty()) {'),
    ("M5 profileSelector_ Props'a baglanmiyor",
     'profileSelector_ = propsProfile;',
     'profileSelector_ = nullptr;'),
    ("M6 rotasyon ENTER ile uygulamiyor",
     'QObject::connect(propsRotation, &QDoubleSpinBox::editingFinished, this,\n'
     '                     applyRotationField);',
     '/* rotasyon ENTER yok */;'),
    ("M7 comboProfileName metni degil adi okumali (davranis)",
     'const QVariant data = combo->currentData();\n'
     '    if (data.isValid()) return data.toString().trimmed().toStdString();\n'
     '    return combo->currentText().trimmed().toStdString(); // elle yazilan ad',
     'return combo->currentText().toStdString();'),
    ("M8 refresh secim yokken aktif profili gostermiyor",
     'const QString pending = QString::fromStdString(app_.pendingProfileName());',
     'const QString pending;'),
    ("M9 odak tuvale donmuyor (kisayollar calismaz)",
     'if (HWND canvas = app_.activeCanvasHandle()) SetFocus(canvas);',
     '/* odak geri verilmiyor */;'),
]

caught = 0
for label, old, new in MUTATIONS:
    if old not in ORIG:
        print(f"SETUP HATASI: parca bulunamadi -> {label}")
        src.write_text(ORIG, encoding="utf-8")
        raise SystemExit(1)
    try:
        src.write_text(ORIG.replace(old, new, 1), encoding="utf-8")
        rc = subprocess.run([sys.executable,
                             str(root / "tests/run_props_profile_panel.py"),
                             str(build)], capture_output=True).returncode
    finally:
        src.write_text(ORIG, encoding="utf-8")
    if rc != 0:
        caught += 1
        print(f"  YAKALANDI  {label}")
    else:
        print(f"  KACIRILDI! {label}")

# Referans: mutasyonsuz halde test GECMELI
ref = subprocess.run([sys.executable, str(root / "tests/run_props_profile_panel.py"),
                      str(build)], capture_output=True).returncode

disk_ok = src.read_text(encoding="utf-8") == ORIG
print(f"mutasyon: {caught}/{len(MUTATIONS)} yakalandi; "
      f"referans {'GECTI' if ref == 0 else 'DUSTU'}; dosya geri yazildi={disk_ok}")
raise SystemExit(0 if (caught == len(MUTATIONS) and ref == 0 and disk_ok) else 1)
