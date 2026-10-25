#!/usr/bin/env python3
"""Polar tracking F10: kaynak-sekli + davranis testi.

Iki katman:
  1) KAYNAK SEKLI: F10 bir SISTEM TUSUDUR (MSDN: "the function keys other
     than F10") — Windows onu WM_KEYDOWN ile GONDERMEZ. Bu yuzden F10
     isleyicisi WM_SYSKEYDOWN icinde OLMALI. Bu kontrol, dalin yeniden
     yanlis case'e kaymasini engeller (gercek regresyon buydu: F10 dalı
     WM_KEYDOWN'daydi ve hic calismiyordu).
  2) DAVRANIS: gercek Application::togglePolarTracking() govdesi
     src/application.cpp'den VERBATIM cikarilip sahte bir Application
     sinifiyla derlenir; toggle semantigi (ortho kapanir, kapatinca
     tracking temizlenir, yenileme cagrilari) dogrulanir.

Gerçek Win32 mesaj dagitimini KANITLAMAZ (F10 -> WM_SYSKEYDOWN eslemesi
isletim sisteminin isidir, MSDN kaynagiyla gerekcelendirilmistir).
Kullanim: python3 tests/run_polar_f10.py [build-directory]
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
app = (root / "src/application.cpp").read_text(encoding="utf-8")

failures = []


def top_level(sig):
    """`sig(...) { ... }` govdesini (satir basindan ilk `}`'a kadar) dondur."""
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^}", app, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


# ---------------------------------------------------------------- 1) sekil
canvas = top_level("LRESULT Application::handleCanvasMessage")
sys_i = canvas.find("case WM_SYSKEYDOWN:")
key_i = canvas.find("case WM_KEYDOWN:")
if sys_i < 0:
    failures.append("handleCanvasMessage icinde WM_SYSKEYDOWN yok")
elif not (sys_i < key_i):
    failures.append("WM_SYSKEYDOWN, WM_KEYDOWN'dan SONRA (sirada hata)")
else:
    syskey_block = canvas[sys_i:key_i]
    if "VK_F10" not in syskey_block:
        failures.append("WM_SYSKEYDOWN icinde VK_F10 isleyicisi YOK "
                        "(F10 sistem tusudur, WM_KEYDOWN'a hic dusmez)")
    if "togglePolarTracking" not in syskey_block:
        failures.append("WM_SYSKEYDOWN VK_F10 dali togglePolarTracking() cagirmali")
    if "case WM_KEYDOWN:" not in canvas:
        failures.append("handleCanvasMessage icinde WM_KEYDOWN yok")

outer = top_level("LRESULT Application::handleMessage")
if "WM_SYSKEYDOWN" not in outer:
    failures.append("dis pencere proc'unda (handleMessage) WM_SYSKEYDOWN yok — "
                    "canvas odagi yokken F10 kaybolur")
elif "VK_F10" not in outer:
    failures.append("dis pencere proc'unda VK_F10 dali yok")

# Yedek WM_KEYDOWN dali da tek kaynaga gitmeli (satir ici kopya kalmamali).
if re.search(r"wParam == VK_F10\)\s*\{\s*polarTrackingEnabled_ = !", canvas):
    failures.append("WM_KEYDOWN'da satir ici polar toggle kopyasi var — "
                    "togglePolarTracking() kullanilmali")

# ------------------------------------------------------------ 2) davranis
m = re.search(r"^void Application::togglePolarTracking\(.*?^}", app, re.M | re.S)
assert m, "togglePolarTracking bulunamadi"
body = m.group()

shell = r'''
#include <cstdio>
#include <cstdlib>
using HWND = void*;
struct POINT { long x{}, y{}; };
namespace mm {
struct Application {
    bool polarTrackingEnabled_ = false;
    bool orthoEnabled_ = false;
    POINT cursorScreen_{};
    int trackCleared = 0, hoverCalls = 0, controlsCalls = 0, statusCalls = 0,
        invalidateCalls = 0;
    void clearTemporaryTracking() { ++trackCleared; }
    void updateHover(int, int) { ++hoverCalls; }
    void updateControls() { ++controlsCalls; }
    void updateStatus() { ++statusCalls; }
    void invalidateCanvas() { ++invalidateCalls; }
    void togglePolarTracking();
};
''' + body + r'''
} // namespace mm

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main() {
    using mm::Application;
    {
        Application a;
        a.cursorScreen_ = POINT{123, 456};
        a.togglePolarTracking();
        CHECK(a.polarTrackingEnabled_, "acilmali");
        CHECK(!a.orthoEnabled_, "acilinca ortho kapanmali");
        CHECK(a.trackCleared == 0, "acilirken tracking TEMIZLENMEMELI (TP noktalari kaybolur)");
        CHECK(a.hoverCalls == 1 && a.controlsCalls == 1 && a.statusCalls == 1 &&
              a.invalidateCalls == 1, "acilista 4 yenileme de cagrilmali");
        a.togglePolarTracking();
        CHECK(!a.polarTrackingEnabled_, "kapanmali");
        CHECK(a.trackCleared == 1, "kapaninca tracking temizlenmeli");
        CHECK(a.hoverCalls == 2 && a.controlsCalls == 2 && a.statusCalls == 2 &&
              a.invalidateCalls == 2, "kapanista da 4 yenileme cagrilmali");
    }
    {
        // Ortho acikken polar acilirsa ortho kapanir (ayni anda ikisi olmaz).
        Application a;
        a.orthoEnabled_ = true;
        a.togglePolarTracking();
        CHECK(a.polarTrackingEnabled_ && !a.orthoEnabled_, "polar ortho'yu ezmeli");
    }
    {
        // Ilk cagrida polar zaten kapaliysa temizlenmez, ikinci cagrida temizlenir.
        Application a;
        a.polarTrackingEnabled_ = false;
        a.togglePolarTracking();            // -> acik
        a.togglePolarTracking();            // -> kapali
        a.togglePolarTracking();            // -> acik
        CHECK(a.polarTrackingEnabled_ && a.trackCleared == 1, "sadece kapanista temizlenir");
    }
    std::printf("togglePolarTracking davranis: %s\n", failures ? "HATALI" : "OK");
    return failures ? 1 : 0;
}
'''

build.mkdir(parents=True, exist_ok=True)
generated = build / "polar_f10_generated.cpp"
binary = build / "polar_f10_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                str(generated), "-o", str(binary)], check=True)
rc = subprocess.run([str(binary)]).returncode
if rc != 0:
    failures.append("togglePolarTracking davranis testi dustu")

print("polar-f10 sekil kontrolu: " + ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
