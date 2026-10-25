#!/usr/bin/env python3
"""TP (gecici izleme noktasi) kaydi: TAB ile koy, Esc ile sifirla.

KULLANICI SARTI: "TP noktalarini tab tusu ile koyalim, esc tp leri sifirlar".
Onceden TP'ler 450 ms BEKLEME (dwell) zamanlayicisiyla OTOMATIK kaydediliyordu
(application.cpp, WM_TIMER wParam==5). Bu, noktayi kullanicinin iradesi
disinda kaydediyor ve bekleme sirasinda snap kacirinca yanlis nokta
tutuyordu. Simdi: TP yalnizca TAB ile konur (imlecin altindaki HAM snap
noktasi), Esc hepsini siler.

Iki katman:
  1) KAYNAK SEKLI: dwell mekanigi (WM_TIMER 5 / SetTimer 450 /
     temporaryPointDwellCandidate_) TAMAMEN kalkmis olmali; TAB dali hem
     canvas proc WM_KEYDOWN'unda hem dis pencere proc'unda olmali; Esc yolu
     clearTemporaryTracking() cagirmali; onCharacter '\\t' karakterini yok
     saymali (TranslateMessage WM_CHAR kopyasi uretir); WM_GETDLGCODE TAB'i
     bize istemeli.
  2) DAVRANIS: gercek Application::acquireTemporaryTrackingPoint() ve
     clearTemporaryTracking() govdeleri application.cpp'den VERBATIM
     cikarilip sahte bir Application ile derlenir: polar kapaliyken / snap
     yokken kayit YOK, ayni nokta iki kez yazilmaz, 8 nokta siniri (en eski
     duser), Esc semantigi (liste + aday + kilitler temiz).

Gercek Windows/Qt tus dagitimini KANITLAMAZ (TAB'in canvas proc'a ulasmasi
isletim sisteminin isi); ilk gercek deneme kullanicinin makinesinde yapilir.
Kullanim: python3 tests/run_tp_tab.py [build-directory]
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
app = (root / "src/application.cpp").read_text(encoding="utf-8")
header = (root / "include/model_maker/application.hpp").read_text(encoding="utf-8")

failures = []


def top_level(sig):
    """`sig(...) { ... }` govdesini (satir basindan ilk `}`'a kadar) dondur."""
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^}", app, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


# ---------------------------------------------------------------- 1) sekil
if "void acquireTemporaryTrackingPoint();" not in header:
    failures.append("application.hpp acquireTemporaryTrackingPoint bildirmiyor")
if "temporaryAcquireCandidate_" not in header:
    failures.append("application.hpp TP aday alani (temporaryAcquireCandidate_) yok")

if "void Application::acquireTemporaryTrackingPoint(" not in app:
    failures.append("application.cpp acquireTemporaryTrackingPoint tanimlamiyor")

# Dwell mekanigi tamamen kalkmali.
if "temporaryPointDwellCandidate_" in app or "temporaryPointDwellCandidate_" in header:
    failures.append("eski dwell adayi (temporaryPointDwellCandidate_) hala kodda "
                    "— TP artik TAB ile konur, otomatik bekleme OLMAMALI")
if re.search(r"SetTimer\(window_,\s*5,", app):
    failures.append("TP dwell zamanlayicisi (SetTimer(window_, 5, ...)) hala kuruluyor")
if re.search(r"wParam == 5\)", app):
    failures.append("WM_TIMER wParam==5 (dwell) dali hala var")

canvas = top_level("LRESULT Application::handleCanvasMessage")
key_i = canvas.find("case WM_KEYDOWN:")
if key_i < 0:
    failures.append("handleCanvasMessage icinde WM_KEYDOWN yok")
else:
    key_block = canvas[key_i:]
    if "VK_TAB" not in key_block:
        failures.append("WM_KEYDOWN icinde VK_TAB dali YOK (TP kaydi TAB ile olmali)")
    elif "acquireTemporaryTrackingPoint" not in key_block[
            key_block.find("VK_TAB"):key_block.find("VK_TAB") + 300]:
        failures.append("TAB dali acquireTemporaryTrackingPoint() cagirmali")

# TAB'in odak degistirmesi engelli mi?
if "WM_GETDLGCODE" not in canvas or "DLGC_WANTTAB" not in canvas:
    failures.append("canvas proc WM_GETDLGCODE'da DLGC_WANTTAB dondurmeli "
                    "(yoksa TAB odak degistirmeye gider)")

outer = top_level("LRESULT Application::handleMessage")
if "VK_TAB" not in outer:
    failures.append("dis pencere proc'unda VK_TAB dali yok — odak canvas'ta "
                    "degilken TAB kaybolur")

# Esc TP'leri sifirlar.
esc_i = canvas.find("wParam == VK_ESCAPE")
if esc_i < 0:
    failures.append("handleCanvasMessage icinde VK_ESCAPE dali yok")
elif "clearTemporaryTracking()" not in canvas[esc_i:esc_i + 900]:
    failures.append("Esc dali clearTemporaryTracking() cagirmali (TP'ler sifirlanmali)")

# Dwell kaldirilinca satir ici 'wParam == 5' blogu da WM_TIMER'da kalmamali;
# TP adayi updateHover'da tazeleniyor olmali.
hover = top_level("void Application::updateHover")
if "temporaryAcquireCandidate_ = rawSnap;" not in hover:
    failures.append("updateHover TP adayini (temporaryAcquireCandidate_) "
                    "ham snap'ten tazelemiyor")

# WM_CHAR kopyasi: '\t' yok sayilmali.
char_fn = top_level("void Application::onCharacter")
if "L'\\t'" not in char_fn.split("L'\\r'")[0]:
    failures.append("onCharacter '\\t' karakterini yok saymiyor "
                    "(Tab, WM_CHAR kopyasiyla metin girisini bozar)")

# ------------------------------------------------------------ 2) davranis
body = top_level("void Application::acquireTemporaryTrackingPoint")
clear_body = top_level("void Application::clearTemporaryTracking")

shell = r'''
#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>
using HWND = void*;
struct POINT { long x{}, y{}; };
using UINT = unsigned;
constexpr UINT MB_ICONWARNING = 0x00000030u;
int g_beeps = 0;
void MessageBeep(UINT) { ++g_beeps; }
void KillTimer(HWND, unsigned long long) {}

namespace mm {

struct Vec3 {
    double x{}, y{}, z{};
    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct SnapResult { Vec3 point{}; int type{}; double distance{}; };
struct TrackingGuide { Vec3 from{}, to{}; };

struct Application {
    bool polarTrackingEnabled_ = false;
    HWND window_ = nullptr;
    POINT cursorScreen_{};
    std::vector<Vec3> temporaryTrackingPoints_;
    std::vector<TrackingGuide> temporaryTrackingGuides_;
    std::vector<Vec3> temporaryDerivedPoints_;
    std::vector<Vec3> temporaryPerpendicularPoints_;
    std::optional<SnapResult> temporaryAcquireCandidate_;
    bool polarTrackingLocked_ = false;
    bool temporaryTrackingLocked_ = false;
    std::wstring lastStatus;
    int hoverCalls = 0, statusCalls = 0, invalidateCalls = 0;
    void publishStatus(const std::wstring& text) { lastStatus = text; }
    void updateHover(int, int) { ++hoverCalls; }
    void updateStatus() { ++statusCalls; }
    void invalidateCanvas() { ++invalidateCalls; }
    void acquireTemporaryTrackingPoint();
    void clearTemporaryTracking();
};

''' + body + "\n" + clear_body + r'''
}  // namespace mm

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool has(const std::wstring& hay, const wchar_t* needle) {
    return hay.find(needle) != std::wstring::npos;
}

int main() {
    using namespace mm;

    // 1) Polar kapali: hicbir sey kaydedilmez, uyarilir.
    {
        Application a; a.polarTrackingEnabled_ = false;
        g_beeps = 0;
        a.temporaryAcquireCandidate_ = SnapResult{Vec3{1.0, 2.0, 3.0}, 3, 0.0};
        a.acquireTemporaryTrackingPoint();
        CHECK(a.temporaryTrackingPoints_.empty(), "polar kapali: TP kaydedilmemeli");
        CHECK(g_beeps == 1, "polar kapali: uyari bip'i");
        CHECK(has(a.lastStatus, L"polar tracking kapali"), "polar kapali: durum mesaji");
    }
    // 2) Polar acik ama imlecte snap yok: kayit yok, uyari var.
    {
        Application a; a.polarTrackingEnabled_ = true;
        a.temporaryAcquireCandidate_.reset();
        g_beeps = 0;
        a.acquireTemporaryTrackingPoint();
        CHECK(a.temporaryTrackingPoints_.empty(), "snap yok: TP kaydedilmemeli");
        CHECK(g_beeps == 1, "snap yok: uyari bip'i");
        CHECK(has(a.lastStatus, L"snap noktasi yok"), "snap yok: durum mesaji");
    }
    // 3) Polar acik + snap var: HAM snap noktasi kaydedilir, ekran tazelenir.
    {
        Application a; a.polarTrackingEnabled_ = true;
        a.temporaryAcquireCandidate_ = SnapResult{Vec3{10.0, 20.0, 0.0}, 1, 0.0};
        g_beeps = 0;
        a.acquireTemporaryTrackingPoint();
        const Vec3 expected{10.0, 20.0, 0.0};
        CHECK(a.temporaryTrackingPoints_.size() == 1, "TP kaydedilmeli");
        CHECK(a.temporaryTrackingPoints_.front() == expected,
              "TP ham snap noktasina oturmali (hover_ degil)");
        CHECK(g_beeps == 0, "basarili kayitta bip YOK");
        CHECK(has(a.lastStatus, L"TP1"), "durum: TP1 kaydedildi");
        CHECK(has(a.lastStatus, L"Esc"), "durum: Esc ile temizlenir bilgisi");
        CHECK(a.hoverCalls == 1 && a.statusCalls == 1 && a.invalidateCalls == 1,
              "kayit sonrasi turetilmis noktalar icin tazeleme (hover/status/redraw)");
    }
    // 4) Ayni nokta iki kez yazilmaz.
    {
        Application a; a.polarTrackingEnabled_ = true;
        a.temporaryAcquireCandidate_ = SnapResult{Vec3{5.0, 5.0, 0.0}, 1, 0.0};
        a.acquireTemporaryTrackingPoint();
        a.acquireTemporaryTrackingPoint();
        CHECK(a.temporaryTrackingPoints_.size() == 1, "ayni nokta TEKRAR kaydedilmemeli");
        CHECK(has(a.lastStatus, L"zaten"), "durum: nokta zaten kayitli");
        CHECK(a.invalidateCalls == 1, "tekrarda gereksiz redraw yok");
    }
    // 5) 8 nokta siniri: en eski duser (FIFO).
    {
        Application a; a.polarTrackingEnabled_ = true;
        for (int i = 0; i < 9; ++i) {
            a.temporaryAcquireCandidate_ =
                SnapResult{Vec3{static_cast<double>(i), 0.0, 0.0}, 1, 0.0};
            a.acquireTemporaryTrackingPoint();
        }
        CHECK(a.temporaryTrackingPoints_.size() == 8, "en fazla 8 TP tutulmali");
        const Vec3 oldest{1.0, 0.0, 0.0};
        const Vec3 newest{8.0, 0.0, 0.0};
        CHECK(a.temporaryTrackingPoints_.front() == oldest, "9. noktada EN ESKI (i=0) duser");
        CHECK(a.temporaryTrackingPoints_.back() == newest, "en yeni TP (i=8) sona eklenir");
    }
    // 6) Esc semantigi = clearTemporaryTracking(): liste + aday + kilitler.
    {
        Application a; a.polarTrackingEnabled_ = true;
        a.temporaryAcquireCandidate_ = SnapResult{Vec3{1.0, 1.0, 0.0}, 1, 0.0};
        a.acquireTemporaryTrackingPoint();
        a.temporaryTrackingGuides_.push_back(TrackingGuide{});
        a.temporaryDerivedPoints_.push_back(Vec3{2.0, 2.0, 0.0});
        a.temporaryPerpendicularPoints_.push_back(Vec3{3.0, 3.0, 0.0});
        a.polarTrackingLocked_ = true;
        a.temporaryTrackingLocked_ = true;
        a.clearTemporaryTracking();
        CHECK(a.temporaryTrackingPoints_.empty(), "Esc: TP listesi bosalmali");
        CHECK(!a.temporaryAcquireCandidate_, "Esc: aday sifirlanmali");
        CHECK(a.temporaryTrackingGuides_.empty() && a.temporaryDerivedPoints_.empty() &&
              a.temporaryPerpendicularPoints_.empty(), "Esc: kilavuz/turetilmis noktalar bosalmali");
        CHECK(!a.polarTrackingLocked_ && !a.temporaryTrackingLocked_, "Esc: kilitler kalkmali");
        // Temizlikten sonra TAB ile yeniden konabilmeli.
        a.temporaryAcquireCandidate_ = SnapResult{Vec3{7.0, 7.0, 0.0}, 1, 0.0};
        a.acquireTemporaryTrackingPoint();
        CHECK(a.temporaryTrackingPoints_.size() == 1, "Esc sonrasi TP yeniden konabilmeli");
    }
    std::printf("TP TAB/Esc davranis: %s\n", failures ? "HATALI" : "OK");
    return failures ? 1 : 0;
}
'''

build.mkdir(parents=True, exist_ok=True)
generated = build / "tp_tab_generated.cpp"
binary = build / "tp_tab_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                str(generated), "-o", str(binary)], check=True)
rc = subprocess.run([str(binary)]).returncode
if rc != 0:
    failures.append("TP TAB/Esc davranis testi dustu")

print("tp-tab sekil kontrolu: " + ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
