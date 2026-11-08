#!/usr/bin/env python3
"""Props paneli = TEK profil kaynagi (UI-3a QLineEdit) + ENTER dogrudan uygulama.

KULLANICI SARTI (uc parca; UI-34 kesit 1 yeni sozlesme):
  1) "sol ust ribbon daki profil secimi kaldirilsin"
  2) "sagdaki props ta hangi ozellikler varsa ona gore profil cizilsin direk"
  3) "secim yapip props penceresinden degisiklik yapilan satirda entera
     basinca da direk ozelligi degissin"

UI-3a sozlesmesi (eski combo yerine):
  - Ribbon'da ayri profil cubugu YOK (profileBar, profileToolbar,
    addToolBarBreak, profileSelectorHint, "profileSelector" objectName,
    createToolbar icinde app_.profileNames() yok).
  - Tek profil alani OZELLIKLER PANELINDEKI QLineEdit
    (`profileSelector_ = propsPanel_->profileEdit();`).
  - X tusu yolu: createDockPanels icinde setProfilePickerCallback panele
    odaklanir; "panel.focusProfile" handler'i propsPanel_->focusProfileEdit().
  - Profil katalogu BIR KEZ kurulur (setProfileCatalog); refresh listeyi
    YENIDEN KURMAZ (kullanicinin sectigi deger kaybolmaz).
  - Normalizasyon buyuk/kucuk harf duyarsiz: canonicalName lambda'si
    QString::compare(..., Qt::CaseInsensitive) kullanir.
  - Uygulama tek yol (applyProfileNow): setPendingProfileName + secim varsa
    assignProfileToSelection; katalogda olmayan ad REDDEDILIR (setProfileValid
    false + durum mesaji); odak tuvale doner.
  - ENTER/tamamlama panel icinde (properties_panel.cpp, returnPressed ->
    onProfileEnter) — dogrudan uygular.

Iki katman:
  1) KAYNAK SEKLI (qt_main_window.cpp + properties_panel.cpp).
  2) DAVRANIS: canonicalName lambda govdesi VERBATIM cikarilip sahte
     QString/QStringList sinirinda derlenir: harf duyarsiz eslesme, bos
     katalog, bos sonuc.

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
panel = (root / "src/properties_panel.cpp").read_text(encoding="utf-8")

failures = []


def block_for(text, sig):
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^\}", text, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


toolbar = block_for(qt, "void QtMainWindow::createToolbar")
panels = block_for(qt, "void QtMainWindow::createDockPanels")

# ------------------------------------------------- 1) ribbon profil cubugu gitti
for gone in ["profileBar", "profileToolbar", "profileSelectorHint",
             'setObjectName("profileSelector")', '"Çelik Profil"']:
    if gone in qt:
        failures.append(f"ribbon profil cubugu kaldirilmamis — '{gone}' hala var")

if "addToolBarBreak" in qt:
    failures.append("addToolBarBreak hala var (profil cubugu ribbon'a ikinci satir aciyordu)")

if "profileNames()" in toolbar:
    failures.append("createToolbar hala app_.profileNames() cagiriyor — profil UI ribbon'da kalmis")

# ------------------------------------------ 2) tek alan ozellikler panelindeki QLineEdit
if "profileSelector_ = propsPanel_->profileEdit();" not in panels:
    failures.append("profileSelector_ paneldeki QLineEdit'e baglanmamis "
                    "(X tusu/odak Props'a gitmez)")

if "setProfilePickerCallback" not in panels:
    failures.append("setProfilePickerCallback createDockPanels icinde kurulmamis "
                    "(Application'in kendi CmdProfileButton yolu panele gitmez)")

if "propsPanel_->focusProfileEdit()" not in qt:
    failures.append("panel.focusProfile / X tusu focusProfileEdit() kullanmiyor")

# Katalog bir kez kurulur: refresh setProfileCatalog'i YENIDEN CAGIRMAZ.
if qt.count("setProfileCatalog(") != 1:
    failures.append(f"setProfileCatalog beklenen 1 kez kurulmuyor "
                    f"(adet={qt.count('setProfileCatalog(')}) — refresh katalogu "
                    "yeniden kurarsa kullanicinin degeri kaybolur)")

if "propsPanel_->validateProfile" not in panels:
    failures.append("profil validasyon callback'i kurulmamis (katalog disi ad reddedilmez)")

# Normalizasyon: buyuk/kucuk harf duyarsiz.
if "QString::compare(item, text, Qt::CaseInsensitive)" not in panels:
    failures.append("canonicalName harf duyarsiz karsilastirma yapmiyor "
                    "('heb240' -> 'HEB240' calismaz)")

# ------------------------------------------------- 3) ENTER = dogrudan uygula (tek yol)
if "app_.setPendingProfileName(name);" not in panels:
    failures.append("profil alani cizim icin aktif profili (pending) ayarlamiyor")
if "app_.assignProfileToSelection(name);" not in panels:
    failures.append("profil alani secime profil atamiyor")
if "app_.hasSelection()" not in panels:
    failures.append("profil alani secim var/yok ayrimi yapmiyor")
if "propsPanel_->setProfileValid(false);" not in panels:
    failures.append("katalogda olmayan profil REDDEDILMIYOR (sessizce yanlis profil)")

# ENTER baglantisi panel icinde (properties_panel.cpp): returnPressed -> onProfileEnter.
if not re.search(r"connect\(profileEdit_,\s*&QLineEdit::returnPressed", panel):
    failures.append("profil satirinda ENTER (returnPressed) baglantisi yok — "
                    "satirda Enter profili uygulamiyor")

if "if (HWND canvas = app_.activeCanvasHandle()) SetFocus(canvas);" not in panels:
    failures.append("profil seciminden sonra odak tuvale donmuyor "
                    "(cizim kisayollari L/M/D calismaz)")

# ------------------------------------------------------------ 2) DAVRANIS
m = re.search(r"const auto canonicalName = \[\]\(const QStringList& catalog,.*?"
              r"return QString\(\);\s*\};", panels, re.S)
assert m, "canonicalName lambda govdesi cikarilamadi"
body = m.group()

shell = r'''
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace Qt {
enum CaseSensitivity { CaseSensitive, CaseInsensitive };
}

struct QString {
    std::string s;
    QString() = default;
    explicit QString(std::string v) : s(std::move(v)) {}
    QString trimmed() const {
        const std::size_t b = s.find_first_not_of(" \t\n\r");
        if (b == std::string::npos) return QString();
        const std::size_t e = s.find_last_not_of(" \t\n\r");
        return QString(s.substr(b, e - b + 1));
    }
    static int compare(const QString& a, const QString& b, Qt::CaseSensitivity cs) {
        if (cs == Qt::CaseSensitive) {
            return a.s == b.s ? 0 : (a.s < b.s ? -1 : 1);
        }
        std::string x = a.s, y = b.s;
        for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : y) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return x == y ? 0 : (x < y ? -1 : 1);
    }
    bool isEmpty() const { return s.empty(); }
    bool operator==(const QString& o) const { return s == o.s; }
};

struct QStringList {
    std::vector<QString> items;
    std::vector<QString>::const_iterator begin() const { return items.begin(); }
    std::vector<QString>::const_iterator end() const { return items.end(); }
};

namespace {
''' + body + r'''
}  // namespace

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main() {
    // Kucuk harfle yazilan ad katalogdaki gercek yazima normalleşir.
    {
        QStringList catalog;
        catalog.items = {QString("HEB240"), QString("IPE300")};
        CHECK(canonicalName(catalog, QString("heb240")) == QString("HEB240"),
              "'heb240' -> 'HEB240' (harf duyarsiz)");
    }
    // Buyuk/kucuk karisik de calisir.
    {
        QStringList catalog;
        catalog.items = {QString("S355J2"), QString("S450")};
        CHECK(canonicalName(catalog, QString("s355j2")) == QString("S355J2"),
              "'s355j2' -> 'S355J2'");
    }
    // Katalogda yok: bos doner -> alan kirmizi, uygulanmaz.
    {
        QStringList catalog;
        catalog.items = {QString("HEB240")};
        CHECK(canonicalName(catalog, QString("XXX999")) == QString(""),
              "katalogda olmayan ad bos donmeli");
    }
    // Bos katalog guvenli.
    {
        QStringList catalog;
        CHECK(canonicalName(catalog, QString("HEB240")) == QString(""),
              "bos katalogda bos donmeli");
    }
    std::printf("canonicalName davranisi: %s\n", failures ? "HATALI" : "OK");
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
    failures.append("canonicalName davranis testi dustu")

print("props profil paneli sekil kontrolu: " + ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
