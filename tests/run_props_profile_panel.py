#!/usr/bin/env python3
"""Props paneli = TEK profil kaynagi + satirda ENTER ile dogrudan uygulama.

KULLANICI SARTI (uc parca):
  1) "sol ust ribbon daki profil secimi kaldirilsin"
  2) "sagdaki props ta hangi ozellikler varsa ona gore profil cizilsin direk"
  3) "secim yapip props penceresinden de degisiklik yapilan satirda entera
      basinca da direk ozelligi degissin"

Iki katman:
  1) KAYNAK SEKLI (qt_main_window.cpp):
     - Ribbon'da ayri profil cubugu KALMAMALI (`profileBar`, `profileToolbar`,
       `addToolBarBreak`, `profileSelectorHint`, `"profileSelector"` objectName,
       createToolbar icinde `app_.profileNames()` yok).
     - Bu secim Props paneline tasinmali: `profileSelector_ = propsProfile;`,
       `setProfilePickerCallback` createDockPanels icinde.
     - Liste BIR KEZ kurulur (yer tutucu + katalog, UserRole = ad); refresh
       `propsProfile->clear()` ILE YENIDEN KURMAMALI (kullanicinin sectigi
       deger kaybolmasin) ve secim yokken AKTIF profili gostermeli.
     - Profil uygulama yolu HEM aktif profili (pending) HEM secimi ayarlar ve
       liste secimi (`textActivated`) ile ENTER (`returnPressed`) ayni yolu
       kullanir. Rotasyon satiri `editingFinished` (ENTER) ile uygular.
  2) DAVRANIS: gercek `comboProfileName()` govdesi VERBATIM cikarilip sahte
     QString/QVariant/QComboBox sinirinda derlenir; yer tutucu (veri = bos)
     profilsiz, oge verisi profil adi, elle yazilan ad metinden okunur.

Kullanim: python3 tests/run_props_profile_panel.py [build-directory]
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
qt = (root / "src/qt_main_window.cpp").read_text(encoding="utf-8")

failures = []


def block_for(sig):
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^\}", qt, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


toolbar = block_for("void QtMainWindow::createToolbar")
panels = block_for("void QtMainWindow::createDockPanels")

# ------------------------------------------------- 1) ribbon profil cubugu gitti
for gone in ["profileBar", "profileToolbar", "profileSelectorHint",
             'setObjectName("profileSelector")', '"Çelik Profil"']:
    if gone in qt:
        failures.append(f"ribbon profil cubugu kaldirilmamis — '{gone}' hala var")

if "addToolBarBreak" in qt:
    failures.append("addToolBarBreak hala var (profil cubugu ribbon'a ikinci satir aciyordu)")

if "profileNames()" in toolbar:
    failures.append("createToolbar hala app_.profileNames() cagiriyor — profil UI ribbon'da kalmis")

# ------------------------------------------------- 2) secim Props paneline tasindi
if "profileSelector_ = propsProfile;" not in panels:
    failures.append("profileSelector_ propsProfile'a baglanmamis (X tusu/odak Props'a gitmez)")

if "setProfilePickerCallback" not in panels:
    failures.append("setProfilePickerCallback createDockPanels icinde kurulmamis")

# Liste bir kez kurulur: yer tutucu (UserRole bos) + katalog adlari (UserRole ad)
if "propsProfile->addItem(profileNoneLabel(), QString());" not in panels:
    failures.append("profil combo'suna 'profilsiz' yer tutucusu eklenmemis")
if not re.search(r"propsProfile->addItem\(QString::fromStdString\(name\),\s*"
                 r"QString::fromStdString\(name\)\)", panels):
    failures.append("profil adlari oge verisiyle (Qt::UserRole) eklenmemis")

if "propsProfile->clear()" in qt:
    failures.append("refresh hala propsProfile->clear() ile listeyi yeniden kuruyor "
                    "(kullanicinin sectigi aktif profil kaybolur)")

if "pendingProfileName()" not in panels:
    failures.append("refresh secim yokken AKTIF (pending) profili gostermiyor")

# ------------------------------------------------- 3) ENTER = dogrudan uygula
if "app_.setPendingProfileName(name);" not in panels:
    failures.append("profil alani cizim icin aktif profili (pending) ayarlamiyor")
if "app_.assignProfileToSelection(name);" not in panels:
    failures.append("profil alani secime profil atamiyor")
if "app_.hasSelection()" not in panels:
    failures.append("profil alani secim var/yok ayrimi yapmiyor")

if "QLineEdit* profileEdit = propsProfile->lineEdit()" not in panels:
    failures.append("profil satirinin line edit'i alinmiyor")
if "QObject::connect(profileEdit, &QLineEdit::returnPressed, this, applyProfileField)" not in panels:
    failures.append("profil satirinda ENTER (returnPressed) baglantisi yok — "
                    "satirda Enter profili uygulamiyor")
if "applyProfileField" not in panels:
    failures.append("profil icin tek uygulama yolu (applyProfileField) yok")

if "&QDoubleSpinBox::editingFinished" not in panels:
    failures.append("rotasyon satiri ENTER (editingFinished) ile uygulamiyor")

if "comboProfileName(propsProfile)" not in panels:
    failures.append("kolon butonu profil adini comboProfileName ile okumuyor")

if "if (HWND canvas = app_.activeCanvasHandle()) SetFocus(canvas);" not in panels:
    failures.append("profil seciminden sonra odak tuvale donmuyor "
                    "(cizim kisayollari L/M/D calismaz)")

# ------------------------------------------------------------ 2) DAVRANIS
m = re.search(r"^std::string comboProfileName\(const QComboBox\* combo\) \{.*?^\}",
              qt, re.M | re.S)
assert m, "comboProfileName govdesi cikarilamadi"
body = m.group()

shell = r'''
#include <cstdio>
#include <string>

// Sahte Qt siniri: comboProfileName yalniz currentData()/isValid()/toString()/
// currentText()/trimmed()/toStdString() kullanir.
struct QString {
    std::string s;
    QString() = default;
    explicit QString(std::string v) : s(std::move(v)) {}
    static QString fromUtf8(const char* p) { return QString(p ? p : ""); }
    QString trimmed() const {
        const std::size_t b = s.find_first_not_of(" \t\n\r");
        if (b == std::string::npos) return QString();
        const std::size_t e = s.find_last_not_of(" \t\n\r");
        return QString(s.substr(b, e - b + 1));
    }
    std::string toStdString() const { return s; }
    bool isEmpty() const { return s.empty(); }
};
struct QVariant {
    bool valid = false;
    QString v;
    QVariant() = default;
    explicit QVariant(QString x) : valid(true), v(std::move(x)) {}
    bool isValid() const { return valid; }
    QString toString() const { return v; }
};
struct QComboBox {
    QVariant data{};
    QString text{};
    QVariant currentData() const { return data; }
    QString currentText() const { return text; }
};

namespace {
''' + body + r'''
}  // namespace

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main() {
    // Yer tutucu: oge verisi GECERLI ama bos -> profilsiz.
    {
        QComboBox c;
        c.data = QVariant(QString(""));
        c.text = QString("— Profilsiz (d\u00fcz \u00e7izgi) —");
        CHECK(comboProfileName(&c) == "", "yer tutucu profilsiz olmali ('' donmeli)");
    }
    // Katalog ogesi: veri = profil adi.
    {
        QComboBox c;
        c.data = QVariant(QString("IPE200"));
        c.text = QString("IPE200");
        CHECK(comboProfileName(&c) == "IPE200", "katalog ogesi adi veriden okunmali");
    }
    // Elle yazilan ad: veri YOK -> metinden okunur ve kirpilir.
    {
        QComboBox c;
        c.text = QString("  HEA160  ");
        CHECK(comboProfileName(&c) == "HEA160", "elle yazilan ad kirpilarak okunmali");
    }
    // Elle yazilan ama bos/bosluk: profilsiz.
    {
        QComboBox c;
        c.text = QString("   ");
        CHECK(comboProfileName(&c) == "", "bosluk metin profilsiz sayilmali");
    }
    // Bos combo (guvenli).
    {
        QComboBox c;
        CHECK(comboProfileName(&c) == "", "veri yok + metin bos -> '' (cokme yok)");
    }
    CHECK(comboProfileName(nullptr) == "", "null combo guvenli olmali");
    std::printf("comboProfileName davranisi: %s\n", failures ? "HATALI" : "OK");
    return failures ? 1 : 0;
}
'''

build.mkdir(parents=True, exist_ok=True)
generated = build / "props_profile_panel_generated.cpp"
binary = build / "props_profile_panel_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                "-Werror", str(generated), "-o", str(binary)], check=True)
rc = subprocess.run([str(binary)]).returncode
if rc != 0:
    failures.append("comboProfileName davranis testi dustu")

print("props profil paneli sekil kontrolu: " + ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
