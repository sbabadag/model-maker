#!/usr/bin/env python3
"""Props paneli YALNIZ UYGULA'da yazar (A — veri butunlugu).

KULLANICI BULGUSU (A):
  1) Renk secildikten sonra renk kendiliginden degisiyor (kararsiz).
  2) Aci (Rotasyon) panelden degistirilince sonradan aci degisiyor.

Kok neden (kanitlanan):
  - Rotasyon spin kutusu `editingFinished` ile uygulaniyordu; `editingFinished`
    ODak kaybinda (secim degistikten SONRA) tetiklenir ve `setSelectedEntity
    ProfileRotation` o anki secime uygulanir -> deger YANLIS nesneye yazilir.
  - UYGULA butonu rotasyon + kolon kotlarini KOSULSUZ uyguluyordu; coklu
    secimde ilk secilen nesnenin degerleri diger secili nesnelere yazilir.

Kural (kullanici): panel yalniz UYGULA'da yazar; doldurma sirasinda sinyaller
bloklu (QSignalBlocker); coklu secimde DEGISTIRILMEYEN alan yazilmaz.

Bu test KAYNAK SEKLI (qt_main_window.cpp) denetler:
  - Rotasyon `editingFinished` baglantisi YOK (yalniz valueChanged -> kirli).
  - `applyRotationField` kirli bayragini kontrol eder (`if (!*rotationDirty) return;`).
  - `applyColumnFields` kirli bayragini kontrol eder (`if (!*columnDirty) return;`).
  - Doldurma (refresh) tum alanlarda QSignalBlocker kullanir.

Kullanim: python3 tests/run_props_panel_apply_only.py [build-directory]
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
qt = (root / "src/qt_main_window.cpp").read_text(encoding="utf-8")

failures = []


def block_for(sig):
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^\}", qt, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


panels = block_for("void QtMainWindow::createDockPanels")

# 1) Rotasyon: editingFinished YOK, valueChanged kirli isaretler.
if "&QDoubleSpinBox::editingFinished" in panels:
    # Kolon kotlari editingFinished kullanmaya DEVAM eder; rotasyon etmemeli.
    # Rotasyonun valueChanged baglantisi varken editingFinished baglantisi
    # rotasyona ozel olmamali. Burada kirli-tabanli gecit denetlenir.
    pass  # kolon icin mesru; asil denetim asagida

if "auto rotationDirty = std::make_shared<bool>(false);" not in panels:
    failures.append("rotasyon kirli bayragi (rotationDirty) tanimlanmamis")

if "QOverload<double>::of(&QDoubleSpinBox::valueChanged)" not in panels:
    failures.append("rotasyon valueChanged baglantisi yok (kirli isareti kurulmamis)")

if "[rotationDirty](double) { *rotationDirty = true; }" not in panels:
    failures.append("rotasyon valueChanged kirli bayragini isaretlemiyor")

if "if (!*rotationDirty) return;" not in panels:
    failures.append("applyRotationField kirli bayragini kontrol etmiyor "
                    "(degistirilmeyen rotasyon UYGULA'da yazilir)")

# 2) Rotasyona editingFinished ile OTOMATIK uygulama geri donmemeli:
#    applyRotationField yalniz UYGULA lambda'si icinden cagrilmali.
rot_conn = re.findall(r"QObject::connect\(propsRotation,[^;]*;", panels, re.S)
for c in rot_conn:
    if "editingFinished" in c:
        failures.append("rotasyon hala editingFinished ile otomatik uygulaniyor "
                        "(odak kaybi yanlis nesneye yazar)")

# 3) Kolon: kirli bayragi + kontrol.
if "auto columnDirty = std::make_shared<bool>(false);" not in panels:
    failures.append("kolon kirli bayragi (columnDirty) tanimlanmamis")

if "[this, propsMaterial, columnDirty](double v)" not in panels:
    failures.append("kolon valueChanged kirli bayragini isaretlemiyor")

if "if (!*columnDirty) return;" not in panels:
    failures.append("applyColumnFields kirli bayragini kontrol etmiyor "
                    "(degistirilmeyen kolon kotu UYGULA'da yazilir)")

# 4) Doldurma (refresh) tum alanlarda QSignalBlocker kullanir (geri yazma yok).
for field in ["profileBlocker", "layerBlocker", "colorBlocker", "lineTypeBlocker",
              "materialBlocker", "rotationBlocker", "columnTopBlocker",
              "columnBottomBlocker"]:
    if field not in panels:
        failures.append(f"doldurma {field} ile sinyal bloklamiyor")

print("props paneli 'yalniz UYGULA yazar' sekil kontrolu: " +
      ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
