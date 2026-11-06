#!/usr/bin/env python3
"""Kaynak-sekli kapisi: Ctrl+Tab gorunus gecisinin TEK yol (kayit) oldugunu
ve Win32/Qt iki yonlendiricinin dogru baglandigini dogrular.

QtMainWindow + Application Win32 yollari Linux'ta derlenmez; bu yuzden
davranisi kaynak sekliyle kilitleriz (bir dal geriye donerse test duser):

  1) Application::dispatchKeyChord TAB'i ERKEN REDDETMEZ (yalniz ESC kapili):
     "Ctrl+Tab"/"Ctrl+Shift+Tab" kayittan cozulebilir.
  2) Iki WM_KEYDOWN dalinda da VK_TAB yalniz CTRL basili DEGILKEN TP kaydina
     gider; Ctrl+Tab dispatchKeyChord'a duser.
  3) Qt: view.next -> cycleViewWindow(+1), view.previous -> cycleViewWindow(-1).
  4) cycleViewWindow olusturma sirasina gore dongu yapar (CreationOrder +
     setActiveSubWindow).

Kullanim: python3 tests/run_view_tab_cycle.py [kaynak-kok]
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
app = (root / "src/application.cpp").read_text(encoding="utf-8")
qt = (root / "src/qt_main_window.cpp").read_text(encoding="utf-8")

failures = []


def check(ok, label):
    print(("PASS " if ok else "FAIL ") + label)
    if not ok:
        failures.append(label)


# 1) dispatchKeyChord: TAB erken reddedilmez.
check("if (virtualKey == VK_ESCAPE) return false;" in app,
      "dispatchKeyChord yalniz ESC'yi erken reddeder")
check("virtualKey == VK_ESCAPE || virtualKey == VK_TAB" not in app,
      "TAB artık dispatchKeyChord'tan erken donmez (Ctrl+Tab kayittan cozulur)")

# 2) Iki WM_KEYDOWN dali da VK_TAB'i Ctrl-gate'ler.
check(app.count("wParam == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000)") == 2,
      "iki WM_KEYDOWN dalinda da VK_TAB yalniz Ctrl'sizken TP'ye gider")

# 3) Qt isleyici baglari.
check('h["view.next"] = [this]() { cycleViewWindow(+1); };' in qt,
      "view.next -> cycleViewWindow(+1)")
check('h["view.previous"] = [this]() { cycleViewWindow(-1); };' in qt,
      "view.previous -> cycleViewWindow(-1)")

# 4) cycleViewWindow olusturma sirasi + aktif pencere.
check("subWindowList(QMdiArea::CreationOrder)" in qt,
      "cycleViewWindow olusturma sirasina gore donguler")
check("mdiArea_->setActiveSubWindow(target)" in qt,
      "cycleViewWindow aktif alt pencereyi degistirir")

if failures:
    print(f"view tab cycle: {len(failures)} kaynak-sekli kontrolu basarisiz")
    sys.exit(1)
print("view tab cycle: kaynak sekli dogrulandi")
