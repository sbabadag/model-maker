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

# --- 1b) Komutlar SINIFLARA ayrilmis: her kategori kendi menusunde ---
menu_decls = [
    ("constructionMenu", 'QMenu* constructionMenu = appMenu_->addMenu('),
    ("viewMenu", 'QMenu* viewMenu = appMenu_->addMenu('),
    ("workPlaneMenu", 'QMenu* workPlaneMenu = appMenu_->addMenu('),
    ("viewsMenu", 'QMenu* viewsMenu = appMenu_->addMenu('),
    ("gridMenu", 'QMenu* gridMenu = appMenu_->addMenu('),
    ("windowMenu", 'QMenu* windowMenu = appMenu_->addMenu('),
]
positions = {}
for name, decl in menu_decls:
    p = qt.find(decl)
    positions[name] = p
    if p < 0:
        failures.append(f"{name} menusu bulunamadi")

names = [n for n, _ in menu_decls]
blocks = {}
for i, name in enumerate(names):
    p = positions[name]
    if p < 0:
        blocks[name] = ""
        continue
    nxt = next((positions[names[j]] for j in range(i + 1, len(names))
                if positions[names[j]] >= 0), None)
    blocks[name] = qt[p:nxt] if nxt is not None else qt[p:]

for needle, block, why in [
    ("startConstructionPlaneCommand", "constructionMenu", "konstrüksiyon düzlemi kendi menüsünde değil"),
    ("toggleConstructionVisibility", "constructionMenu", "konstrüksiyon göster/gizle kendi menüsünde değil"),
    ("startFittingCommand", "constructionMenu", "Fitting kendi menüsünde değil"),
    ("startPlaneCutCommand", "constructionMenu", "Plane Cut kendi menüsünde değil"),
    ("startLineCutCommand", "constructionMenu", "Line Cut kendi menüsünde değil"),
    ("startPolygonCutCommand", "constructionMenu", "Polygon Cut kendi menüsünde değil"),
    ("startPartCutCommand", "constructionMenu", "Part Cut kendi menüsünde değil"),
    ("zoomExtents2D", "viewMenu", "Zoom Extents Görünüm menüsünde değil"),
    ("startZoomWindow2D", "viewMenu", "Zoom Window Görünüm menüsünde değil"),
    ("toggle3DView", "viewMenu", "2B/3B Görünüm menüsünde değil"),
    ("setStandardView(StandardView::Front)", "viewMenu", "standart görünüş (Ön) Görünüm menüsünde değil"),
    ("setStandardView(StandardView::Isometric)", "viewMenu", "standart görünüş (ISO) Görünüm menüsünde değil"),
    ("resetView", "viewMenu", "Görünümü Sıfırla Görünüm menüsünde değil"),
    ("startRotateCenterCommand", "viewMenu", "dönüş merkezi Görünüm menüsünde değil"),
    ("clearRotateCenter", "viewMenu", "dönüş merkezini temizle Görünüm menüsünde değil"),
    ("startWorkPlaneCommand", "workPlaneMenu", "çalışma düzlemi kendi menüsünde değil"),
    ("resetWorkPlane", "workPlaneMenu", "düzlemi sıfırla kendi menüsünde değil"),
    ("toggleWorkPlaneGhost", "workPlaneMenu", "hayalet düzlem kendi menüsünde değil"),
    ("setViewToWorkPlane", "workPlaneMenu", "UCS'ye dik görünüş kendi menüsünde değil"),
    ("setGlobalWorkPlane", "workPlaneMenu", "global düzlemler kendi menüsünde değil"),
    ("promptSaveNamedView", "workPlaneMenu", "görünümü adlandır kendi menüsünde değil"),
    ("startTwoPointViewCommand", "viewsMenu", "2 noktalı görünüş kendi menüsünde değil"),
    ("startPlanViewCommand", "viewsMenu", "plan görünüşü kendi menüsünde değil"),
    ("startThreePointViewCommand", "viewsMenu", "3 noktalı görünüş kendi menüsünde değil"),
    ("startPartPlaneViewCommand", "viewsMenu", "parça düzlemi görünüşleri kendi menüsünde değil"),
    ("promptCreateGrid", "gridMenu", "grid oluştur kendi menüsünde değil"),
    ("promptGridManager", "gridMenu", "grid yöneticisi kendi menüsünde değil"),
]:
    if needle not in blocks[block]:
        failures.append(why)

# Sinif disina tasma olmamali: calisma duzlemi / gorunus / grid / konstrüksiyon
# komutlari Görünüm menusune karismamali.
for needle, why in [
    ("startWorkPlaneCommand", "çalışma düzlemi Görünüm menüsünde (kendi menüsüne ayrılmalı)"),
    ("startTwoPointViewCommand", "görünüş penceresi Görünüm menüsünde (kendi menüsüne ayrılmalı)"),
    ("promptCreateGrid", "grid Görünüm menüsünde (kendi menüsüne ayrılmalı)"),
    ("startConstructionPlaneCommand", "konstrüksiyon Görünüm menüsünde (kendi menüsüne ayrılmalı)"),
]:
    if needle in blocks["viewMenu"]:
        failures.append(why)

# --- 1c) Ribbon: her kategori kendi sekmesinde (menüyle paralel) ---
tab_names = ["Konstrüksiyon", "Görünüm", "Çalışma Düzlemi", "Görünüşler", "Grid"]
tab_positions = [qt.find(f'beginTab("{n}")') for n in tab_names]
tab_blocks = {}
for i, n in enumerate(tab_names):
    p = tab_positions[i]
    if p < 0:
        failures.append(f"ribbon sekmesi bulunamadi: {n}")
        tab_blocks[n] = ""
        continue
    nxt = next((tab_positions[j] for j in range(i + 1, len(tab_names))
                if tab_positions[j] >= 0), None)
    tab_blocks[n] = qt[p:nxt] if nxt is not None else qt[p:]

for needle, tab, why in [
    ("app_.startConstructionPlaneCommand()", "Konstrüksiyon", "ribbon: konstrüksiyon düzlemi yok"),
    ("app_.startFittingCommand()", "Konstrüksiyon", "ribbon: Fitting yok"),
    ("app_.startPlaneCutCommand()", "Konstrüksiyon", "ribbon: Plane Cut yok"),
    ("app_.startLineCutCommand()", "Konstrüksiyon", "ribbon: Line Cut yok"),
    ("app_.startPolygonCutCommand()", "Konstrüksiyon", "ribbon: Polygon Cut yok"),
    ("app_.startPartCutCommand()", "Konstrüksiyon", "ribbon: Part Cut yok"),
    ("app_.toggle3DView()", "Görünüm", "ribbon: 2B/3B yok"),
    ("app_.zoomExtents2D()", "Görünüm", "ribbon: Extents yok"),
    ("app_.startZoomWindow2D()", "Görünüm", "ribbon: Zoom Window yok"),
    ("app_.resetView()", "Görünüm", "ribbon: sıfırla yok"),
    ("StandardView::Front", "Görünüm", "ribbon: yön menüsü yok"),
    ("StandardView::Isometric", "Görünüm", "ribbon: yön menüsü eksik"),
    ("app_.startRotateCenterCommand()", "Görünüm", "ribbon: dönüş merkezi yok"),
    ("app_.clearRotateCenter()", "Görünüm", "ribbon: merkez temizle yok"),
    ("app_.startWorkPlaneCommand()", "Çalışma Düzlemi", "ribbon: çalışma düzlemi yok"),
    ("app_.resetWorkPlane()", "Çalışma Düzlemi", "ribbon: düzlem sıfırla yok"),
    ("app_.toggleWorkPlaneGhost()", "Çalışma Düzlemi", "ribbon: hayalet yok"),
    ("app_.setViewToWorkPlane()", "Çalışma Düzlemi", "ribbon: UCS dik yok"),
    ("app_.setGlobalWorkPlane(0)", "Çalışma Düzlemi", "ribbon: global düzlem yok"),
    ("promptSaveNamedView()", "Çalışma Düzlemi", "ribbon: görünüm adlandır yok"),
    ("app_.startTwoPointViewCommand()", "Görünüşler", "ribbon: 2 noktalı görünüş yok"),
    ("app_.startPlanViewCommand()", "Görünüşler", "ribbon: plan görünüşü yok"),
    ("app_.startThreePointViewCommand()", "Görünüşler", "ribbon: 3 noktalı görünüş yok"),
    ("promptCreateGrid()", "Grid", "ribbon: grid oluştur yok"),
    ("promptGridManager()", "Grid", "ribbon: grid yöneticisi yok"),
]:
    if needle not in tab_blocks[tab]:
        failures.append(why)

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
