#!/usr/bin/env python3
"""Props paneli YALNIZ UYGULA'da yazar (A — veri butunlugu).

KULLANICI BULGUSU (A):
  1) Renk secildikten sonra renk kendiliginden degisiyor (kararsiz).
  2) Aci (Rotasyon) panelden degistirilince sonradan aci degisiyor.

Kok neden (kanitlanan, eski panel):
  - Rotasyon spin kutusu `editingFinished` ile uygulaniyordu; `editingFinished`
    ODak kaybinda (secim degistikten SONRA) tetiklenir ve deger YANLIS nesneye
    yazilirdi.
  - UYGULA butonu alanlari KOSULSUZ uyguluyordu; coklu secimde degistirilmeyen
    alan diger secili nesnelere yazilirdi.

Kural (kullanici): panel yalniz UYGULA'da yazar; doldurma sirasinda sinyaller
bloklu (QSignalBlocker); coklu secimde DEGISTIRILMEYEN alan yazilmaz.

UI-3a sozlesmesi (panel PropertiesPanel'e tasindi; ayni kurallar gecerli):
  - Rotasyonda editingFinished ILE OTOMATIK UYGULAMA YOK (yalniz valueChanged
    -> rotationDirty_ kirli bayragi; panel icinde).
  - UYGULA (qt_main_window.cpp onApplyClicked) kirli bayraklarini kontrol
    eder: isRotationDirty/isColumnDirty/isNameDirty/isFinishDirty;
    uygulanmayan alan icin nullptr gider (applySelectedEntityBatch).
  - Doldurma (refresh setter'lari) PropertiesPanel icinde QSignalBlocker
    altindadir (kirli bayrak tetiklenmez).
  - Kolon kotlari editingFinished (ENTER) ile DOGRUDAN uygulanir
    (applyColumnLevels — kullanicinin "satirda ENTER" kurali korunur);
    rotasyon icin ENTER yok.

Kullanim: python3 tests/run_props_panel_apply_only.py [build-directory]
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
qt = (root / "src/qt_main_window.cpp").read_text(encoding="utf-8")
panel = (root / "src/properties_panel.cpp").read_text(encoding="utf-8")

failures = []


def block_for(text, sig):
    sig = sig.rstrip("(")
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^\}", text, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


panels = block_for(qt, "void QtMainWindow::createDockPanels")

# 1) Rotasyon: editingFinished ile OTOMATIK uygulama YOK.
rot_conn = re.findall(r"QObject::connect\(rotationSpin_,[^;]*;", panel, re.S)
if not rot_conn:
    failures.append("rotasyon spin baglantisi bulunamadi (properties_panel.cpp)")
for c in rot_conn:
    if "editingFinished" in c:
        failures.append("rotasyon hala editingFinished ile otomatik uygulaniyor "
                        "(odak kaybi yanlis nesneye yazar)")

# Rotasyon valueChanged kirli bayragi isaretler (panel icinde).
if "[this](double) {\n            rotationDirty_ = true;" not in panel:
    failures.append("rotasyon valueChanged rotationDirty_ kirli bayragini isaretlemiyor")

# 2) UYGULA kirli bayraklarini kontrol eder; temiz alan nullptr gider.
if "propsPanel_->isRotationDirty()" not in panels:
    failures.append("UYGULA rotasyon kirli bayragini kontrol etmiyor "
                    "(degistirilmeyen rotasyon UYGULA'da yazilir)")
if "propsPanel_->isColumnDirty() ? &top : nullptr" not in panels:
    failures.append("UYGULA kolon kotu kirli bayragini kontrol etmiyor "
                    "(degistirilmeyen kolon kotu UYGULA'da yazilir)")
if "propsPanel_->isNameDirty()" not in panels:
    failures.append("UYGULA ad kirli bayragini kontrol etmiyor")
if "propsPanel_->isFinishDirty()" not in panels:
    failures.append("UYGULA yuzey islemi kirli bayragini kontrol etmiyor")

# 3) Kolon: ENTER (editingFinished) dogrudan uygular (kullanicinin ENTER kurali).
col_conn = re.findall(r"QObject::connect\(column(?:Top|Bottom)Spin_,[^;]*;", panel, re.S)
if not any("editingFinished" in c for c in col_conn):
    failures.append("kolon kotlarinda ENTER (editingFinished) dogrudan uygulamasi yok")

# 4) Doldurma (refresh setter'lari) QSignalBlocker altinda (geri yazma yok).
for setter in ["void PropertiesPanel::setProfile(",
               "void PropertiesPanel::setMaterial(",
               "void PropertiesPanel::setName(",
               "void PropertiesPanel::setFinish(",
               "void PropertiesPanel::setClassIndex(",
               "void PropertiesPanel::setLayer(",
               "void PropertiesPanel::setLineType(",
               "void PropertiesPanel::setRotation(",
               "void PropertiesPanel::setColumnLevels("]:
    fn = block_for(panel, setter)
    if "QSignalBlocker" not in fn:
        failures.append(f"doldurma {setter.split('::')[2].split('(')[0]} "
                        "QSignalBlocker kullanmiyor (kirli bayrak tetiklenir)")

# 5) UYGULA sonrasi kirli bayraklar temizlenir (A kurali: tek yazim).
if "propsPanel_->clearDirtyFlags();" not in panels:
    failures.append("UYGULA sonrasi clearDirtyFlags cagrisi yok")

print("props paneli 'yalniz UYGULA yazar' sekil kontrolu: " +
      ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
