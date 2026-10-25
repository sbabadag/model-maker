#!/usr/bin/env python3
"""GORUNUM KOMUTLARI: tek menu + tek ribbon sekmesi, ve PLAN gorunusu (Z kotu).

KULLANICI SARTI (iki parca):
  1) "view eklemeye PLAN XY de belirtilen Z koordinatinda ekle" — gorunus
     ekleme akisina Z KOTU parametresi eklenir: gorunus duzlemi XY'ye PARALEL
     olur ve Z kotu NE GORUNUR'u belirler (Tekla seviye plani). Kot 0 veya
     NEGATIF olabilir; bu yuzden `requestDoubleParameter` (>0 sart) DEGIL,
     ayri `requestLevelParameter` kullanilir.
  2) "tum goruntu ile ilgili komutlar bir menu ve ribbonda toplansin" — TUM
     gorunum komutlari (yakinlastirma, yon, sifirlama, donus merkezi, calisma
     duzlemi, hayalet, gorunus pencereleri, grid) TEK "Gorunum" menusunde ve
     TEK "Gorunum" ribbon sekmesinde toplanir; komut baska yere dagilmaz.

Iki katman:
  1) KAYNAK SEKLI: menu blogu kendi icinde kapali (windowMenu'dan SONRA
     `viewMenu->` cagrisi YOK), ribbon sekmesi menüdeki komutlarin TAMAMINI
     icerir, plan gorunusu aksiyonu/dugmesi var, baslik ve derinlik etiketleri
     plan gorunusunu ayirt eder.
  2) DAVRANIS: gercek Application::startPlanViewCommand() ve
     requestLevelParameter() govdeleri application.cpp'den VERBATIM cikarilip
     sahte bir Application + GERCEK view_definition.hpp ile derlenir: callback
     yokken komut baslamaz, iptal/bozuk deger kabul edilmez, 0 ve NEGATIF kot
     kabul edilir, uretilen gorunus PLAN'dir (normal +Z, uzunluk 0), yarim
     kalmis nokta secimi once temizlenir.

Gercek Windows/Qt menu/dugme davranisini KANITLAMAZ; ilk deneme kullanicinin
makinesinde yapilir. Kullanim: python3 tests/run_view_menu.py [build-directory]
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
viewdef = (root / "include/model_maker/view_definition.hpp").read_text(encoding="utf-8")
twopoint = (root / "src/two_point_view.cpp").read_text(encoding="utf-8")

failures = []


def top_level(text, sig):
    """`sig(...) { ... }` govdesini (satir basindan ilk `}`'a kadar) dondur."""
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^}", text, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


# ---------------------------------------------------------------- 1) sekil
# --- 1a) Plan gorunusu cekirdegi ---
if "viewFromPlanLevel" not in viewdef:
    failures.append("view_definition.hpp viewFromPlanLevel yok")
if "isPlanView" not in viewdef:
    failures.append("view_definition.hpp isPlanView yok")
if "void startPlanViewCommand();" not in header:
    failures.append("application.hpp startPlanViewCommand bildirmiyor")
if "requestLevelParameter" not in header:
    failures.append("application.hpp requestLevelParameter bildirmiyor")
if "void Application::startPlanViewCommand(" not in app:
    failures.append("application.cpp startPlanViewCommand tanimlamiyor")

plan_body = top_level(app, "void Application::startPlanViewCommand")
for needle, why in [
    ("requestLevelParameter", "plan gorunusu kotu requestLevelParameter ile sorulmali"),
    ("viewFromPlanLevel", "plan gorunusu viewFromPlanLevel ile uretilmeli"),
    ("twoPointViewCallback_", "plan gorunusu AYNI gorunus callback akisini kullanmali"),
]:
    if needle not in plan_body:
        failures.append(why)

# Yarim kalmis nokta secimi once temizlenmeli (yoksa arka planda asili kalir).
if "workPlanePicking_" not in plan_body or "cancelWorkPlaneCommand" not in plan_body:
    failures.append("startPlanViewCommand yarim kalmis nokta secimini temizlemiyor")

level_body = top_level(app, "std::optional<double> Application::requestLevelParameter")
if "value <= 0.0" in level_body or "value < 0.0" in level_body:
    failures.append("requestLevelParameter kotu 0/negatif diye REDDEDIYOR "
                    "(kot 0 veya negatif olabilir)")

# --- 1b) TEK KAYNAK komut tablosu: her kategori kendi menusu + AYNI ribbon sekmesi ---
# KULLANICI SARTI (2026-10-23): "Menüler çok karıştı hepsini kendi ilgili ribbon
# ve menülerine taşır mısın". Menu ve ribbon commandCategories() tablosundan
# uretilir; bir komut yalniz bir kategoride, bir kez tanimlanir.
table = top_level(qt, "std::vector<QtMainWindow::CommandCategory> QtMainWindow::commandCategories")

cat_markers = [
    ("Dosya", "    // ---- DOSYA", '{QStringLiteral("&Dosya"), QString(),'),
    ("Çizim", "    // ---- ÇİZİM", '{QStringLiteral("Çi&zim"), QStringLiteral("Çizim"),'),
    ("Düzenle", "    // ---- DÜZENLE", '{QStringLiteral("Dü&zenle"), QStringLiteral("Düzenle"),'),
    ("Çelik", "    // ---- ÇELİK", '{QStringLiteral("Çe&lik"), QStringLiteral("Çelik"),'),
    ("Konstrüksiyon", "    // ---- KONSTRÜKSİYON", '{QStringLiteral("K&onstrüksiyon"), QStringLiteral("Konstrüksiyon"),'),
    ("Görünüm", "    // ---- GÖRÜNÜM:", '{QStringLiteral("G&örünüm"), QStringLiteral("Görünüm"),'),
    ("Çalışma Düzlemi", "    // ---- ÇALIŞMA DÜZLEMİ", '{QStringLiteral("Çalışma &Düzlemi"), QStringLiteral("Çalışma Düzlemi"),'),
    ("Görünüşler", "    // ---- GÖRÜNÜŞLER", '{QStringLiteral("Görünüş&ler"), QStringLiteral("Görünüşler"),'),
    ("Grid", "    // ---- GRID", '{QStringLiteral("&Grid"), QStringLiteral("Grid"),'),
    ("Pencere", "    // ---- PENCERE", '{QStringLiteral("&Pencere"), QString(),'),
]
positions = []
for name, marker, _ in cat_markers:
    p = table.find(marker)
    if p < 0:
        failures.append(f"komut tablosunda kategori yok: {name}")
    positions.append(p)
if positions != sorted(positions):
    failures.append("komut tablosu kategori sirasi bozuk")
blocks = {}
for i, (name, _, push) in enumerate(cat_markers):
    p = positions[i]
    nxt = next((positions[j] for j in range(i + 1, len(cat_markers)) if positions[j] >= 0), None)
    blocks[name] = table[p:nxt] if (p >= 0 and nxt is not None) else (table[p:] if p >= 0 else "")
    if push not in blocks[name]:
        failures.append(f"{name} kategorisi kendi menu/sekme basligiyla push edilmiyor")

for needle, block, why in [
    ("app_.selectTool(DrawTool::Line)", "Çizim", "Çizgi Çizim'de değil"),
    ("app_.activateDivide()", "Çizim", "Böl (Divide) Çizim'de değil"),
    ("app_.undo()", "Düzenle", "Geri Al Düzenle'de değil"),
    ("app_.redo()", "Düzenle", "Yinele Düzenle'de değil"),
    ("TransformCommand::Move", "Düzenle", "Taşı Düzenle'de değil"),
    ("TransformCommand::Fillet", "Düzenle", "Fillet Düzenle'de değil"),
    ("app_.startBoltCommand()", "Çelik", "Bolt Çelik'te değil"),
    ("startWeldCommand()", "Çelik", "Kaynak Çelik'te değil"),
    ("app_.startEndPlateCommand()", "Çelik", "End Plate Çelik'te değil"),
    ("app_.startFinPlateCommand()", "Çelik", "Fin Plate Çelik'te değil"),
    ("buildComponentCatalog", "Çelik", "Component kataloğu Çelik'te değil"),
    ("importTeklaBridge()", "Çelik", "Tekla Bridge Çelik'te değil"),
    ("saveTeklaBridgeTemplate()", "Çelik", "Kataloğa şablon Çelik'te değil"),
    ("app_.startConstructionPlaneCommand()", "Konstrüksiyon", "konstrüksiyon düzlemi kendi menüsünde değil"),
    ("app_.toggleConstructionVisibility()", "Konstrüksiyon", "konstrüksiyon göster/gizle kendi menüsünde değil"),
    ("app_.startFittingCommand()", "Konstrüksiyon", "Fitting kendi menüsünde değil"),
    ("app_.startPlaneCutCommand()", "Konstrüksiyon", "Plane Cut kendi menüsünde değil"),
    ("app_.startLineCutCommand()", "Konstrüksiyon", "Line Cut kendi menüsünde değil"),
    ("app_.startPolygonCutCommand()", "Konstrüksiyon", "Polygon Cut kendi menüsünde değil"),
    ("app_.startPartCutCommand()", "Konstrüksiyon", "Part Cut kendi menüsünde değil"),
    ("app_.zoomExtents2D()", "Görünüm", "Zoom Extents Görünüm menüsünde değil"),
    ("app_.startZoomWindow2D()", "Görünüm", "Zoom Window Görünüm menüsünde değil"),
    ("app_.toggle3DView()", "Görünüm", "2B/3B Görünüm menüsünde değil"),
    ("StandardView::Front", "Görünüm", "standart görünüş (Ön) Görünüm menüsünde değil"),
    ("StandardView::Isometric", "Görünüm", "standart görünüş (ISO) Görünüm menüsünde değil"),
    ("app_.resetView()", "Görünüm", "Görünümü Sıfırla Görünüm menüsünde değil"),
    ("app_.startRotateCenterCommand()", "Görünüm", "dönüş merkezi Görünüm menüsünde değil"),
    ("app_.clearRotateCenter()", "Görünüm", "dönüş merkezini temizle Görünüm menüsünde değil"),
    ("VisualStyle::Solid", "Görünüm", "görsel stil Görünüm menüsünde değil"),
    ("app_.startWorkPlaneCommand()", "Çalışma Düzlemi", "çalışma düzlemi kendi menüsünde değil"),
    ("app_.resetWorkPlane()", "Çalışma Düzlemi", "düzlemi sıfırla kendi menüsünde değil"),
    ("app_.toggleWorkPlaneGhost()", "Çalışma Düzlemi", "hayalet düzlem kendi menüsünde değil"),
    ("app_.setViewToWorkPlane()", "Çalışma Düzlemi", "UCS'ye dik görünüş kendi menüsünde değil"),
    ("app_.setGlobalWorkPlane(0)", "Çalışma Düzlemi", "global düzlemler kendi menüsünde değil"),
    ("promptSaveNamedView()", "Çalışma Düzlemi", "görünümü adlandır kendi menüsünde değil"),
    ("app_.startTwoPointViewCommand()", "Görünüşler", "2 noktalı görünüş kendi menüsünde değil"),
    ("app_.startPlanViewCommand()", "Görünüşler", "plan görünüşü kendi menüsünde değil"),
    ("app_.startThreePointViewCommand()", "Görünüşler", "3 noktalı görünüş kendi menüsünde değil"),
    ("app_.startPartPlaneViewCommand", "Görünüşler", "parça düzlemi görünüşleri kendi menüsünde değil"),
    ("promptCreateGrid()", "Grid", "grid oluştur kendi menüsünde değil"),
    ("promptGridManager()", "Grid", "grid yöneticisi kendi menüsünde değil"),
    ("closeAllTwoPointViews()", "Pencere", "tüm görünüşleri kapat Pencere'de değil"),
]:
    if needle not in blocks[block]:
        failures.append(why)

# Her komut tabloda TEK KEZ (iki kategoriye dagilmis kopya yok).
for needle in ["app_.startBoltCommand()", "startWeldCommand()", "app_.startEndPlateCommand()",
               "app_.startFinPlateCommand()", "app_.startFittingCommand()", "app_.startPlaneCutCommand()",
               "app_.startPlanViewCommand()", "app_.startTwoPointViewCommand()", "promptCreateGrid()",
               "promptGridManager()", "app_.startWorkPlaneCommand()", "app_.toggle3DView()",
               "app_.activateDivide()", "buildComponentCatalog"]:
    n = table.count(needle)
    if n != 1:
        failures.append(f"komut tabloda {n} kez: {needle} (tam 1 olmali)")

# Sinif disina tasma olmamali.
for needle, block, why in [
    ("app_.startBoltCommand()", "Konstrüksiyon", "Bolt Konstrüksiyon'a karışmış"),
    ("app_.startEndPlateCommand()", "Konstrüksiyon", "End Plate Konstrüksiyon'a karışmış"),
    ("buildComponentCatalog", "Konstrüksiyon", "katalog Konstrüksiyon'a karışmış"),
    ("importTeklaBridge", "Dosya", "Tekla Bridge Dosya'ya karışmış"),
    ("app_.startWorkPlaneCommand()", "Görünüm", "çalışma düzlemi Görünüm menüsünde (kendi menüsüne ayrılmalı)"),
    ("app_.startTwoPointViewCommand()", "Görünüm", "görünüş penceresi Görünüm menüsünde (kendi menüsüne ayrılmalı)"),
    ("promptCreateGrid()", "Görünüm", "grid Görünüm menüsünde (kendi menüsüne ayrılmalı)"),
    ("app_.startConstructionPlaneCommand()", "Görünüm", "konstrüksiyon Görünüm menüsünde (kendi menüsüne ayrılmalı)"),
]:
    if needle in blocks[block]:
        failures.append(why)

# --- 1c) Menu ve ribbon AYNI tablodan uretilir; elle komut eklenmez ---
menus_body = top_level(qt, "void QtMainWindow::createMenus")
if "commandCategories()" not in menus_body or "fillUiMenu(" not in menus_body:
    failures.append("createMenus menüyü komut tablosundan üretmiyor")
if "addAction(" in menus_body:
    failures.append("createMenus'ta elle eklenmiş menü komutu var (tabloya taşınmalı)")

toolbar_body = top_level(qt, "void QtMainWindow::createToolbar")
tabs_part = toolbar_body[:toolbar_body.find("ribbon->addWidget(tabs);")]
if "commandCategories()" not in tabs_part:
    failures.append("ribbon sekmeleri komut tablosundan üretilmiyor")
if "if (cat.tabTitle.isEmpty()) continue;" not in tabs_part:
    failures.append("ribbon yalnız-menü kategorileri ayırt etmiyor")
if tabs_part.count("tabTitle") != 2:  # isEmpty kontrolu + addTab basligi
    failures.append("ribbon bir kategoriyi ayrıca filtreliyor (menü ile ribbon ayrışır)")
for hand in ["addTab(drawTab", "addTab(modTab", "beginTab(", "app_.start", "promptCreateGrid()"]:
    if hand in tabs_part:
        failures.append(f"ribbon'da elle kurulmuş sekme/düğme var: {hand}")
if "fillUiMenu(" not in tabs_part:
    failures.append("ribbon açılır düğmeleri menüyle aynı doldurucuyu kullanmıyor")
if "collectQuickCommands" not in toolbar_body:
    failures.append("Komut ara listesi tablodan üretilmiyor")

# --- 1d) Plan gorunusu baslik/etiket ayrimi ---
open_body = top_level(qt, "void QtMainWindow::openTwoPointView")
if "isPlanView" not in open_body:
    failures.append("openTwoPointView plan gorunusunu ayirt etmiyor")
if "plan, Z = " not in open_body:
    failures.append("plan gorunusu basligi Z kotunu gostermiyor")
if "Plan Görünüşü " not in open_body:
    failures.append("plan gorunusu adi 'Plan Gorunusu' degil")
depth_body = top_level(twopoint, "void TwoPointViewWidget::editDepth")
if "isPlanView" not in depth_body:
    failures.append("derinlik diyalogu plan gorunusunde kot etiketi kullanmiyor")
if "Kotun ÜSTÜ" not in depth_body or "Kotun ALTI" not in depth_body:
    failures.append("plan derinlik etiketleri (kotun ustu/alti) yok")

# ------------------------------------------------------------ 2) davranis
shell = r'''
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "model_maker/view_definition.hpp"

using UINT = unsigned;
constexpr UINT MB_ICONWARNING = 0x00000030u;
static int g_beeps = 0;
static void MessageBeep(UINT) { ++g_beeps; }

namespace mm {

struct Application {
    std::function<bool(const std::wstring&, const std::wstring&, std::wstring&)> parameterRequest_;
    std::function<void(const ViewDefinition&)> twoPointViewCallback_;
    bool workPlanePicking_ = false;
    std::wstring lastStatus;
    int requestCalls = 0, cancelCalls = 0, statusCalls = 0;
    std::vector<ViewDefinition> opened;
    void cancelWorkPlaneCommand() { ++cancelCalls; workPlanePicking_ = false; }
    void publishStatus(const std::wstring& t) { lastStatus = t; ++statusCalls; }
    void openView(const ViewDefinition& v) { opened.push_back(v); }
    std::optional<double> requestLevelParameter(const std::wstring& prompt,
                                               const std::wstring& initial);
    void startPlanViewCommand();
};

''' + level_body + "\n\n" + plan_body + r'''

}  // namespace mm

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool has(const std::wstring& hay, const wchar_t* needle) {
    return hay.find(needle) != std::wstring::npos;
}

int main() {
    using namespace mm;

    // 1) Gorunus callback'i kurulmadan komut baslamaz (bip).
    {
        Application a; g_beeps = 0;
        a.startPlanViewCommand();
        CHECK(a.opened.empty(), "callback yokken gorunus ACILMAMALI");
        CHECK(g_beeps == 1, "callback yokken uyari bip'i");
        CHECK(a.requestCalls == 0, "callback yokken kot SORULMAMALI");
    }
    // 2) Kot sorulur; iptal -> gorunus yok, durum mesaji var.
    {
        Application a; a.twoPointViewCallback_ = [&](const ViewDefinition& v) { a.openView(v); };
        a.parameterRequest_ = [&](const std::wstring&, const std::wstring&, std::wstring&) {
            ++a.requestCalls; return false;   // iptal
        };
        a.startPlanViewCommand();
        CHECK(a.requestCalls == 1, "kot edit box ile sorulmali");
        CHECK(a.opened.empty(), "iptalde gorunus ACILMAMALI");
        CHECK(has(a.lastStatus, L"geçerli"), "iptalde durum mesaji yazilmali");
    }
    // 3) Gecerli kot: PLAN gorunusu uretilir (normal +Z, uzunluk 0).
    {
        Application a; a.twoPointViewCallback_ = [&](const ViewDefinition& v) { a.openView(v); };
        a.parameterRequest_ = [&](const std::wstring&, const std::wstring&, std::wstring& out) {
            ++a.requestCalls; out = L"3600"; return true;
        };
        a.startPlanViewCommand();
        CHECK(a.opened.size() == 1, "gecerli kotta gorunus acilmali");
        if (a.opened.size() == 1) {
            const ViewDefinition& v = a.opened.front();
            CHECK(std::abs(v.origin.z - 3600.0) < 1e-9, "kot gorunusun origin.z'sine yazilmali");
            CHECK(std::abs(v.normal.z - 1.0) < 1e-9, "plan gorunusu normali +Z olmali (XY'ye PARALEL)");
            CHECK(std::abs(v.up.z) < 1e-9 && std::abs(v.up.y - 1.0) < 1e-9,
                  "plan gorunusunde ekran yukarisi +Y olmali");
            CHECK(isPlanView(v), "uretilen gorunus plan gorunusu olmali");
            CHECK(std::abs(v.length) < 1e-9, "plan gorunusunde P1-P2 uzunlugu yok");
        }
    }
    // 4) SIFIR ve NEGATIF kot kabul edilmeli (kot > 0 sarti YOK).
    {
        for (const wchar_t* txt : {L"0", L"-1500", L"-0.5"}) {
            Application a; a.twoPointViewCallback_ = [&](const ViewDefinition& v) { a.openView(v); };
            a.parameterRequest_ = [&](const std::wstring&, const std::wstring&, std::wstring& out) {
                out = txt; return true;
            };
            a.startPlanViewCommand();
            CHECK(a.opened.size() == 1, "0/negatif kot kabul edilmeli");
        }
    }
    // 5) Bozuk deger reddedilir: "12x", "abc", "" (kismi parse = yok).
    {
        for (const wchar_t* txt : {L"12x", L"abc", L""}) {
            Application a; a.twoPointViewCallback_ = [&](const ViewDefinition& v) { a.openView(v); };
            a.parameterRequest_ = [&](const std::wstring&, const std::wstring&, std::wstring& out) {
                out = txt; return true;
            };
            a.startPlanViewCommand();
            CHECK(a.opened.empty(), "bozuk/eksik kot reddedilmeli");
        }
    }
    // 6) Yarim kalmis nokta secimi ONCE temizlenir.
    {
        Application a; a.twoPointViewCallback_ = [&](const ViewDefinition& v) { a.openView(v); };
        a.parameterRequest_ = [&](const std::wstring&, const std::wstring&, std::wstring& out) {
            out = L"7200"; return true;
        };
        a.workPlanePicking_ = true;
        a.startPlanViewCommand();
        CHECK(a.cancelCalls == 1, "yarim kalmis nokta secimi once temizlenmeli");
        CHECK(!a.workPlanePicking_, "nokta secimi kapatilmali");
        CHECK(a.opened.size() == 1, "temizlikten sonra gorunus yine acilmali");
    }
    std::printf("gorunum menu/plan davranis: %s\n", failures ? "HATALI" : "OK");
    return failures ? 1 : 0;
}
'''

build.mkdir(parents=True, exist_ok=True)
generated = build / "view_menu_generated.cpp"
binary = build / "view_menu_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                "-I", str(root / "include"), str(generated), "-o", str(binary)], check=True)
rc = subprocess.run([str(binary)]).returncode
if rc != 0:
    failures.append("gorunum menu/plan davranis testi dustu")

print("gorunum-menu sekil+davranis kontrolu: " + ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
