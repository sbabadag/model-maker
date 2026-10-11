#!/usr/bin/env python3
"""Props paneli: ozelliklerin SECILI elemana uygulanmasi (kolon boyu dahil).

KULLANICI: "props taki ozellikler secili elemana saglikli uygulanmiyor ozellikle
kolon boyunu degistirmiyor hepsini kontrol et".

Olculen kusurlar (gercek regresyon):
  1) Kolon Us / Kolon Alt kutulari YALNIZCA sonraki kolon yerlesimi icin
     saklaniyordu (setColumnProps); secili kolona hic dokunmuyordu ve secim
     degisince secimden de OKUNMUYORDU (hep 8500/0 kaliyordu).
  2) Renk satirinda "ByLayer" secmek hicbir sey yapmiyordu: setCurrentColorChoice
     ByLayer'da erken return ediyordu, secili eleman onceki override renginde
     kaliyordu. Ayrica selectedEntityColorIndex ByLayer icin -1 donuyordu, yani
     panel secili elemanin rengini YANSITMIYORDU.
  3) UYGULA butonu yalnizca rotasyonu uyguluyordu; kolon kotlari uygulanmiyordu.

Uc katman:
  1) KAYNAK SEKLI (qt_main_window.cpp): refresh kolon kotlarini secimden okur;
     ENTER (editingFinished) ve UYGULA ayni tek yolu (applyColumnLevels) kullanir;
     panelin her satiri SECIME uygulanan bir Application API'sine baglidir.
  2) KAYNAK SEKLI (application.cpp): applyColumnLevels tek yol; setSelectedEntity
     ColumnLevels OCC'li VE OCC'siz (stub) tanimli; tek pushUndoSnapshot; stil
     replaceModel'DEN ONCE kopyalanir; occShapes guncellenir; duseysel olmayan
     eleman atlanir.
  3) DAVRANIS: gercek `applyColumnLevels` + `selectedEntityColumnLevels` govdeleri
     VERBATIM cikarilip sahte Application/Document sinirinda derlenir (dusey
     kurali, ust>alt sirasi, secim yokken yalniz bekleyen degerin saklanmasi).
     Ayrica gercek `selectedEntityColorIndex` govdesi ByLayer icin 0 dondurmeli.

Kullanim: python3 tests/run_props_column_levels.py [build-directory]
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
qt = (root / "src/qt_main_window.cpp").read_text(encoding="utf-8")
app = (root / "src/application.cpp").read_text(encoding="utf-8")
header = (root / "include/model_maker/application.hpp").read_text(encoding="utf-8")

failures = []


def block_for(text, sig):
    pattern = r"^" + re.escape(sig) + (r".*?^}" if "(" in sig else r"\(.*?^}")
    m = re.search(pattern, text, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


panels = block_for(qt, "void QtMainWindow::createDockPanels")

# ---------------------------------------------------------------- 1) Qt sekli
if "propsColumnTop" not in header and "columnTopZ" not in header:
    failures.append("application.hpp kolon kot API'si yok")

for captured in ["propsColumnTop", "propsColumnBottom"]:
    if captured not in panels:
        failures.append(f"props panelinde {captured} yok — kolon kot satiri kaldirilmis")

# Refresh secimden OKUMALI (eskiden hic okunmuyordu).
if "app_.selectedEntityColumnLevels(" not in panels:
    failures.append("refresh secili elemanin kolon kotunu OKUMUYOR "
                    "(selectedEntityColumnLevels cagrisi yok)")

# ENTER ve UYGULA ayni tek yolu kullanmali.
if "app_.applyColumnLevels(" not in panels:
    failures.append("props satiri applyColumnLevels() kullanmiyor — "
                    "kolon kotu secime uygulanmaz")
if not re.search(r"connect\(propsColumnTop,\s*&QDoubleSpinBox::editingFinished", panels):
    failures.append("Kolon Ust satirinda ENTER (editingFinished) baglantisi yok")
if not re.search(r"connect\(propsColumnBottom,\s*&QDoubleSpinBox::editingFinished", panels):
    failures.append("Kolon Alt satirinda ENTER (editingFinished) baglantisi yok")

# UYGULA kolon kotlarini da uygulamali (eskiden yalniz rotasyon).
apply_conn = re.search(r"connect\(propsApply,\s*&QPushButton::clicked.*?\}\);", panels, re.S)
if not apply_conn:
    failures.append("UYGULA butonu baglantisi bulunamadi")
else:
    seg = apply_conn.group()
    # CAGRI olmali (yakalama listesinde gecmesi yetmez: [.., applyColumnFields]).
    if "applyColumnFields();" not in seg:
        failures.append("UYGULA kolon kotlarini uygulamiyor (applyColumnFields cagrisi yok)")
    if "applyRotationField();" not in seg:
        failures.append("UYGULA rotasyonu uygulamiyor (applyRotationField cagrisi yok)")

# Her satir SECIME uygulanan bir Application API'sine bagli olmali ("hepsini kontrol et").
PANEL_ROUTES = {
    "profil": "assignProfileToSelection",
    "katman": "setCurrentLayer",
    "renk": "setCurrentColorChoice",
    "cizgi tipi": "setCurrentLineTypeChoice",
    "malzeme": "setSelectedEntityMaterial",
    "rotasyon": "setSelectedEntityProfileRotation",
    "kolon kotu": "applyColumnLevels",
}
for label, api in PANEL_ROUTES.items():
    if api not in panels:
        failures.append(f"props satiri '{label}' secime uygulayan {api}()'ye bagli degil")

# --------------------------------------------------------- 2) application sekli
for need in ["bool Application::selectedEntityColumnLevels(",
             "void Application::setSelectedEntityColumnLevels(",
             "void Application::applyColumnLevels("]:
    if need not in app:
        failures.append(f"application.cpp {need.strip()} tanimlamiyor")

for need in ["selectedEntityColumnLevels(double& top, double& bottom) const;",
             "void setSelectedEntityColumnLevels(double top, double bottom);",
             "void applyColumnLevels(double top, double bottom);"]:
    if need not in header:
        failures.append(f"application.hpp '{need}' bildirmiyor")

# OCC kapaliyken de tanimli olmali (linker hatasi olmasin).
if app.count("void Application::setSelectedEntityColumnLevels(") < 2:
    failures.append("setSelectedEntityColumnLevels yalniz tek (OCC) dala tanimli — "
                    "OCC kapali derlemede linker hatasi verir")

column_fn = block_for(app, "void Application::setSelectedEntityColumnLevels")
if "pushUndoSnapshot()" not in column_fn:
    failures.append("setSelectedEntityColumnLevels undo kaydi acmiyor (Ctrl+Z calismaz)")
if column_fn.count("document_.replaceModel(") < 1:
    failures.append("setSelectedEntityColumnLevels katiyi replaceModel ile degistirmiyor")
if "occShapes_[" not in column_fn:
    failures.append("setSelectedEntityColumnLevels occShapes_ eslemesini guncellemiyor")
# Stil alanlari replaceModel'DEN ONCE kopyalanmali (referans sarkmasi).
copy_pos = column_fn.find("const std::string material = props.material;")
replace_pos = column_fn.find("document_.replaceModel(")
if copy_pos < 0 or replace_pos < 0 or copy_pos > replace_pos:
    failures.append("stil alanlari replaceModel'DEN SONRA kopyalaniyor "
                    "(referans sarkar / stil kaybolur)")
if "extrudeProfileSolid" not in column_fn:
    failures.append("setSelectedEntityColumnLevels katiyi yeniden uretmiyor")

apply_fn = block_for(app, "void Application::applyColumnLevels")
if "setColumnProps(" not in apply_fn:
    failures.append("applyColumnLevels sonraki yerlesim icin degeri saklamiyor")
if "hasSelection()" not in apply_fn:
    failures.append("applyColumnLevels secim var/yok ayrimi yapmiyor")
if "setSelectedEntityColumnLevels(" not in apply_fn:
    failures.append("applyColumnLevels secime uygulamiyor")

# Renk: ByLayer erken return ETMEMELI (regresyon).
color_fn = block_for(app, "void Application::setCurrentColorChoice(int index) noexcept")
erken = re.search(r"if\s*\(\s*!choice\.color", color_fn)
if erken:
    failures.append("setCurrentColorChoice ByLayer'da erken return ediyor — "
                    "ByLayer secmek secili elemani degistirmez (regresyon)")

# --------------------------------------------------------------- 3) davranis
body_levels = block_for(app, "bool Application::selectedEntityColumnLevels")
body_apply = block_for(app, "void Application::applyColumnLevels")
body_color = block_for(app, "int Application::selectedEntityColorIndex")

shell = r'''
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace mm {

struct EntityProperties {
    std::string profileName;
    std::optional<unsigned int> trueColor;
    double axisFromX{}, axisFromY{}, axisFromZ{};
    double axisToX{}, axisToY{}, axisToZ{};
};

struct Faces {
    bool e{true};
    bool empty() const { return e; }
};

struct WireframeModel {
    Faces f;
    EntityProperties p;
    const Faces& faces() const { return f; }
    const EntityProperties& properties() const { return p; }
};

struct Document {
    std::vector<WireframeModel> m;
    const std::vector<WireframeModel>& models() const { return m; }
};

struct ColorChoiceStub { const wchar_t* label; std::optional<unsigned int> color; };
constexpr std::array<ColorChoiceStub, 8> colorChoices{{
    {L"ByLayer", std::nullopt}, {L"White", 0xFFFFFFu}, {L"Red", 0xFF4040u},
    {L"Yellow", 0xFFFF00u}, {L"Green", 0x40FF40u}, {L"Cyan", 0x68CAFFu},
    {L"Blue", 0x4080FFu}, {L"Magenta", 0xFF40FFu}
}};

struct Application {
    std::vector<std::size_t> selectedModels_;
    Document document_;
    double columnTopZ_ = 8500.0, columnBottomZ_ = 0.0;
    std::string columnMaterial_ = "S355";
    int pendingStored = 0;
    double lastAppliedTop = 0.0, lastAppliedBottom = 0.0;
    int appliedCount = 0;

    void setColumnProps(double top, double bottom, std::string material) {
        columnTopZ_ = top; columnBottomZ_ = bottom; columnMaterial_ = std::move(material);
        ++pendingStored;
    }
    bool hasSelection() const { return !selectedModels_.empty(); }
    void setSelectedEntityColumnLevels(double top, double bottom) {
        lastAppliedTop = top; lastAppliedBottom = bottom; ++appliedCount;
    }
    bool selectedEntityColumnLevels(double& top, double& bottom) const;
    void applyColumnLevels(double top, double bottom);
    int selectedEntityColorIndex() const;
};

}  // namespace mm

namespace mm {
''' + body_levels + "\n" + body_apply + "\n" + body_color + r'''
}  // namespace mm

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static mm::WireframeModel column(const char* name, double z0, double z1,
                                 double x0 = 0.0, double x1 = 0.0) {
    mm::WireframeModel m;
    m.f.e = false;                      // faces dolu
    m.p.profileName = name;
    m.p.axisFromX = x0; m.p.axisFromY = 0.0; m.p.axisFromZ = z0;
    m.p.axisToX = x1;   m.p.axisToY = 0.0;   m.p.axisToZ = z1;
    return m;
}

int main() {
    using namespace mm;

    // (1) Dusey kolon: kotlar okunur ve sirayla (kucuk=alt, buyuk=ust) doner.
    {
        Application a;
        a.document_.m = {column("IPE300", 0.0, 8500.0)};
        a.selectedModels_ = {0};
        double top = -1.0, bottom = -1.0;
        CHECK(a.selectedEntityColumnLevels(top, bottom), "dusey kolon okunmali");
        CHECK(top == 8500.0 && bottom == 0.0, "ust/alt kot dogru sirada olmali");
    }
    // (2) Ters cizilmis kolon (ust asagi): yine kucuk=alt, buyuk=ust.
    {
        Application a;
        a.document_.m = {column("IPE300", 7200.0, 1200.0)};
        a.selectedModels_ = {0};
        double top = 0.0, bottom = 0.0;
        CHECK(a.selectedEntityColumnLevels(top, bottom), "ters kolon okunmali");
        CHECK(top == 7200.0 && bottom == 1200.0, "ters kolonda da kucuk=alt, buyuk=ust");
    }
    // (3) YATAY kiris: kolon kotlari ANLAMSIZ -> false (yanlis elemani egmeyelim).
    {
        Application a;
        a.document_.m = {column("IPE300", 0.0, 0.0, 0.0, 6000.0)};
        a.selectedModels_ = {0};
        double top = 0.0, bottom = 0.0;
        CHECK(!a.selectedEntityColumnLevels(top, bottom), "yatay kiris kolon sayilmamali");
    }
    // (4) Profilsiz duz cizgi: kolon degil.
    {
        Application a;
        auto m = column("", 0.0, 5000.0);
        a.document_.m = {m};
        a.selectedModels_ = {0};
        double top = 0.0, bottom = 0.0;
        CHECK(!a.selectedEntityColumnLevels(top, bottom), "profilsiz eleman kolon olmamali");
    }
    // (5) Secim yok: okunmaz, uygulanmaz; yalniz bekleyen deger saklanir.
    {
        Application a;
        a.applyColumnLevels(9000.0, 300.0);
        CHECK(a.pendingStored == 1, "secim yokken deger SONRAKI yerlesim icin saklanmali");
        CHECK(a.appliedCount == 0, "secim yokken secime uygulanmamali");
        CHECK(a.columnTopZ_ == 9000.0 && a.columnBottomZ_ == 300.0,
              "bekleyen kolon kotu guncellenmeli");
    }
    // (6) Secim var: TEK yol hem saklar hem secime uygular (ENTER ve UYGULA ayni yol).
    {
        Application a;
        a.document_.m = {column("IPE300", 0.0, 8500.0)};
        a.selectedModels_ = {0};
        a.applyColumnLevels(4200.0, 0.0);
        CHECK(a.pendingStored == 1, "secim varken de bekleyen deger saklanmali");
        CHECK(a.appliedCount == 1, "secim varken secime uygulanmali");
        CHECK(a.lastAppliedTop == 4200.0 && a.lastAppliedBottom == 0.0,
              "uygulanan kot dogru olmali (kolon boyu degisir)");
    }
    // (6b) NO-OP: secili kolonun kotu zaten ayni ise yeniden uretme ve BOS
    // geri-alma kaydi OLMAZ (spin kutusuna tiklayip baska yere tiklamak).
    {
        Application a;
        a.document_.m = {column("IPE300", 0.0, 8500.0)};
        a.selectedModels_ = {0};
        a.applyColumnLevels(8500.0, 0.0);
        CHECK(a.pendingStored == 1, "no-op'ta bile bekleyen deger saklanir");
        CHECK(a.appliedCount == 0, "kot degismediyse secime UYGULANMAMALI (bos undo yok)");
    }

    // (7) Renk: trueColor YOK = ByLayer -> palette[0] gosterilir (eskiden -1).
    {
        Application a;
        mm::WireframeModel m; m.p.profileName = "";
        m.p.trueColor = std::nullopt;
        a.document_.m = {m};
        a.selectedModels_ = {0};
        CHECK(a.selectedEntityColorIndex() == 0, "ByLayer icin palette[0] donmeli");
    }
    // (8) Renk: bilinen override rengi dogru indeksi verir.
    {
        Application a;
        mm::WireframeModel m;
        m.p.trueColor = 0xFF4040u;  // Red
        a.document_.m = {m};
        a.selectedModels_ = {0};
        CHECK(a.selectedEntityColorIndex() == 2, "Red -> palette[2]");
    }
    // (9) Renk: secim yoksa -1.
    {
        Application a;
        CHECK(a.selectedEntityColorIndex() == -1, "secim yoksa -1");
    }

    std::printf("props kolon kotu davranisi: %s\n", failures ? "HATALI" : "OK");
    return failures ? 1 : 0;
}
'''

build.mkdir(parents=True, exist_ok=True)
generated = build / "props_column_levels_generated.cpp"
binary = build / "props_column_levels_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                "-Werror", str(generated), "-o", str(binary)], check=True)
rc = subprocess.run([str(binary)]).returncode
if rc != 0:
    failures.append("props kolon kotu davranis testi dustu")

print("props kolon kotu sekil kontrolu: " + ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
