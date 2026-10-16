#!/usr/bin/env python3
"""Ekran donus merkezi (R tusu + Ctrl+sol tus): kaynak-sekli + davranis testi.

Kullanici sarti (birebir):
  "Ekran rotate olayinda r tusu bir rotate merkezi belirler hemen ardindan
   kullanici ekrani rotate eder kullanici rotate center ini degistirinceye
   kadar ctrl+mouse top bas ile ekrani o merkez etrafinda rotate eder"

Iki katman:
  1) KAYNAK SEKLI: R dali merkez secmeye gitmeli (Ctrl+R eski "sifirla"yi
     korur); Ctrl+sol tus jesti basmada BEKLEYIP esik asilinca rotasyona
     terfi etmeli (boylece Ctrl+TIK coklu secimi bozulmaz); Esc temizlemeli;
     status/renderer/menu baglantilari yerinde olmali.
  2) DAVRANIS: gercek Application::clearRotateCenter() ve resetView() govdeleri
     src/application.cpp'den VERBATIM cikarilip sahte bir Application
     sinifiyla derlenir ve cagri sozlesmesi (pivot temizleme, kilitli
     pencerede fit, kilitsizde kamera sifirlama) dogrulanir.

Pivotun ekranda SABIT kalmasi ve aci semantigi cekirdek testte olculur:
tests/orbit_pivot_test.cpp (gercek Camera::rotateAbout, 23 kontrol).

Kullanim: python3 tests/run_rotate_center.py [build-directory]
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
camera_hpp = (root / "include/model_maker/camera.hpp").read_text(encoding="utf-8")
camera_cpp = (root / "src/camera.cpp").read_text(encoding="utf-8")
renderer_hpp = (root / "include/model_maker/renderer.hpp").read_text(encoding="utf-8")
renderer_cpp = (root / "src/renderer.cpp").read_text(encoding="utf-8")
qt = (root / "src/qt_main_window.cpp").read_text(encoding="utf-8")

failures = []


def top_level(text, sig):
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^}", text, re.M | re.S)
    assert m, f"{sig} bulunamadi"
    return m.group()


# ------------------------------------------------------------ 1) cekirdek
if "void rotateAbout(const Vec3& pivot" not in camera_hpp:
    failures.append("camera.hpp: rotateAbout bildirimi yok")
if "orbitPivot_" not in camera_hpp:
    failures.append("camera.hpp: orbitPivot_ uyesi yok")

rotate_body = top_level(camera_cpp, "void Camera::rotate")
if "rotateAbout(*orbitPivot_" not in rotate_body:
    failures.append("Camera::rotate pivotu KULLANMIYOR (merkez etrafinda doner)")
rotate_about = top_level(camera_cpp, "void Camera::rotateAbout")
if "mat3mul(rx, mat3mul(R_, rz))" not in rotate_about:
    failures.append("rotateAbout yon guncellemesi rotate() ile ayni kararli formulde degil")
# Kararsiz kurulum geri gelmemeli: w = R^T*rx*R*rz (R^T*R carpimi hatayi buyutur).
if re.search(r"mat3mul\(\s*rt\s*,", rotate_about):
    failures.append("rotateAbout'ta kararsiz w = R^T*rx*R*ry kurulumu var "
                    "(ortonormallik hatasini ~3x buyutur)")
if "previous" not in rotate_about:
    failures.append("rotateAbout onceki matrisi saklamiyor (W^T = R_yeni^T*R_eski icin gerekli)")
for fn in ("void Camera::reset", "void Camera::setView", "void Camera::setViewBasis"):
    if "orbitPivot_.reset()" not in top_level(camera_cpp, fn):
        failures.append(f"{fn}: gorunus degisiminde pivot temizlenmiyor")

# ------------------------------------------------------- 2) R tusu dali
canvas = top_level(app, "LRESULT Application::handleCanvasMessage")
m = re.search(r"wParam == 'R' && mode_ == EditMode::View3D\)\s*\{(.*?)\n        \}", canvas, re.S)
if not m:
    failures.append("R tusu dali (WM_KEYDOWN) bulunamadi")
else:
    branch = m.group(1)
    if "startRotateCenterCommand()" not in branch:
        failures.append("R tusu donus merkezi secmeye gitmiyor")
    if "resetView()" not in branch:
        failures.append("Ctrl+R / kilitli pencere icin resetView() yok "
                        "(eski 'gorunumu sifirla' kaybolur)")
    if "VK_CONTROL" not in branch:
        failures.append("R dali Ctrl'i ayirt etmiyor (Ctrl+R = sifirla)")
    if "viewRotationLocked" not in branch:
        failures.append("R dali kilitli (gorunus) pencereyi ayirt etmiyor")

# ------------------------------------------------- 3) Ctrl+sol tus jesti
down = top_level(app, "void Application::onLeftButtonDown")
ctrl_i = down.find("if (GetKeyState(VK_CONTROL) < 0)")
if ctrl_i < 0:
    failures.append("onLeftButtonDown'da Ctrl dali yok")
else:
    ctrl_block = down[ctrl_i:ctrl_i + 2200]
    if "ctrlRotatePending_ = true" not in ctrl_block:
        failures.append("Ctrl+sol tus basmada beklemeye alinmiyor")
    pending_block = ctrl_block.split("ctrlRotatePending_ = true", 1)[-1]
    if "SetCapture" not in pending_block[:600]:
        failures.append("Ctrl+sol tus jesti SetCapture yapmiyor (pencere disinda kopar)")
    # Bekleme yolundan SONRA eski Ctrl+tik secimi hala olmali.
    after = ctrl_block.split("ctrlRotatePending_ = true", 1)[1]
    if "toggleModelSelection" not in after:
        failures.append("Ctrl+TIK secimi kaldirilmis (bekleme yolundan sonra yok)")

move = top_level(app, "void Application::onMouseMove")
if "ctrlRotatePending_" not in move:
    failures.append("onMouseMove esik kontrolu yok (jest hic terfi etmez)")
if "kCtrlRotateDragThresholdPx" not in move:
    failures.append("onMouseMove esik sabitini kullanmiyor")
if "rotating_ = true" not in move:
    failures.append("onMouseMove esik asilinca rotasyona terfi etmiyor")

up = top_level(app, "void Application::onLeftButtonUp")
if "ctrlRotatePending_" not in up:
    failures.append("onLeftButtonUp bekleyen jesti ele almiyor")
elif "toggleModelSelection" not in up.split("ctrlRotatePending_", 1)[1]:
    failures.append("Ctrl+TIK (esik asilmadi) secimi yapilmiyor")

# Esc + odak/capture degisimi temizlemeli.
if "ctrlRotatePending_ = false" not in canvas.split("case WM_KEYDOWN:")[1][:2000]:
    failures.append("Esc bekleyen Ctrl jestini iptal etmiyor")
if "ctrlRotatePending_ = false" not in canvas.split("case WM_CAPTURECHANGED:")[1][:300]:
    failures.append("WM_CAPTURECHANGED bekleyen jesti temizlemiyor")
if "ctrlRotatePending_" not in top_level(app, "void Application::activateViewport"):
    failures.append("activateViewport bekleyen jesti temizlemiyor")

# ------------------------------------------- 4) nokta secme + komut akisi
commit = top_level(app, "void Application::commitWorkPlanePoint")
rotate_i = commit.find("PointPickPurpose::RotateCenter")
two_i = commit.find("PointPickPurpose::TwoPointView")
if rotate_i < 0:
    failures.append("commitWorkPlanePoint: RotateCenter dali yok")
elif two_i >= 0 and rotate_i > two_i:
    failures.append("RotateCenter dali 2 noktali gorunus dalindan SONRA (once gelmeli)")
elif "camera_.setOrbitPivot(point)" not in commit[:two_i]:
    failures.append("merkez secimi kameraya YAZILMIYOR (setOrbitPivot yok)")

starter = top_level(app, "void Application::startRotateCenterCommand")
if "startWorkPlaneCommand()" not in starter:
    failures.append("startRotateCenterCommand nokta secme akisini paylasmiyor")
if "PointPickPurpose::RotateCenter" not in starter:
    failures.append("startRotateCenterCommand amaci ayarlamiyor")
if "viewRotationLocked" not in starter:
    failures.append("kilitli pencerede merkez secimi engellenmiyor")

# ------------------------------------------------------- 5) gorsel isaret
if "orbitPivotVisible" not in renderer_hpp or "orbitPivot" not in renderer_hpp:
    failures.append("DraftView: pivot isaret alanlari yok")
draft = top_level(app, "DraftView Application::draftView")
if "orbitPivotVisible" not in draft or "hasOrbitPivot" not in draft:
    failures.append("draftView pivotu renderer'a aktarmiyor")
passive = top_level(app, "void Application::paintPassiveViewport")
if "slot.camera.hasOrbitPivot()" not in passive and "slot.camera" not in passive:
    failures.append("pasif pencere pivotu KENDI kamerasindan almiyor")
elif "orbitPivotVisible" not in passive:
    failures.append("pasif pencere pivot isaretini ayarlamiyor")
if "draft.orbitPivotVisible" not in renderer_cpp:
    failures.append("renderer pivot isaretini cizmiyor")
elif "projectPointPrecise(draft.orbitPivot)" not in renderer_cpp:
    failures.append("pivot isareti yuvarlanmamis projeksiyon kullanmiyor "
                    "(zoom'da titrer)")

# ------------------------------------------------------------- 6) menu
if "startRotateCenterCommand" not in qt:
    failures.append("menu: 'Donus Merkezi (R)' baglantisi yok")
if "clearRotateCenter" not in qt:
    failures.append("menu: 'Donus Merkezini Temizle' baglantisi yok")
if "resetView" not in qt:
    failures.append("menu: 'Gorunumu Sifirla (Ctrl+R)' baglantisi yok")
if 'Ctrl+R' not in qt:
    failures.append("menu: Ctrl+R kisayolu tanimli degil")

# ------------------------------------------------------- 7) davranis (govde)
bodies = (top_level(app, "void Application::clearRotateCenter") + "\n" +
          top_level(app, "void Application::resetView"))

shell = r'''
#include <cstdio>
using HWND = void*;
struct POINT { long x{}, y{}; };
namespace mm {
enum class EditMode { Draw2D, View3D };
struct Camera {
    int resetCalls = 0, clearCalls = 0;
    void reset() { ++resetCalls; }
    void clearOrbitPivot() { ++clearCalls; }
};
struct Application {
    Camera camera_;
    bool locked = false;
    HWND canvas_ = reinterpret_cast<HWND>(1);
    EditMode mode_ = EditMode::View3D;
    POINT cursorScreen_{};
    int fitCalls = 0, hoverCalls = 0, controlsCalls = 0, statusCalls = 0, invalidateCalls = 0;
    bool viewRotationLocked() const { return locked; }
    void fitViewport(HWND) { ++fitCalls; }
    void updateHover(int, int) { ++hoverCalls; }
    void updateControls() { ++controlsCalls; }
    void updateStatus() { ++statusCalls; }
    void invalidateCanvas() { ++invalidateCalls; }
    void clearRotateCenter();
    void resetView();
};
''' + bodies + r'''
} // namespace mm

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main() {
    using mm::Application;
    {
        // clearRotateCenter: pivot SILINIR + dort yenileme cagrisi.
        Application a;
        a.clearRotateCenter();
        CHECK(a.camera_.clearCalls == 1, "clearRotateCenter pivotu temizlemeli");
        CHECK(a.camera_.resetCalls == 0, "clearRotateCenter kamerayi sifirlamamali");
        CHECK(a.hoverCalls == 1 && a.controlsCalls == 1 && a.statusCalls == 1 &&
              a.invalidateCalls == 1, "clearRotateCenter: 4 yenileme cagrilmali");
    }
    {
        // resetView (kilitsiz 3B): kamera sifirlanir, fit CAGRILMAZ.
        Application a;
        a.resetView();
        CHECK(a.camera_.resetCalls == 1, "resetView kilitsizde kamerayi sifirlamali");
        CHECK(a.fitCalls == 0, "resetView kilitsizde fitViewport cagirmamali");
        CHECK(a.hoverCalls == 1 && a.controlsCalls == 1 && a.statusCalls == 1 &&
              a.invalidateCalls == 1, "resetView: 4 yenileme cagrilmali");
    }
    {
        // resetView (gorunus penceresi, yon kilitli): yalnizca fit.
        Application a;
        a.locked = true;
        a.resetView();
        CHECK(a.fitCalls == 1, "resetView kilitli pencerede fitViewport cagirmali");
        CHECK(a.camera_.resetCalls == 0, "resetView kilitli pencerede kamerayi "
                                         "sifirlamamali (yon kilidi bozulur)");
    }
    std::printf("donus-merkezi komut davranisi: %s\n", failures ? "HATALI" : "OK");
    return failures ? 1 : 0;
}
'''

build.mkdir(parents=True, exist_ok=True)
generated = build / "rotate_center_generated.cpp"
binary = build / "rotate_center_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                str(generated), "-o", str(binary)], check=True)
rc = subprocess.run([str(binary)]).returncode
if rc != 0:
    failures.append("clearRotateCenter/resetView davranis testi dustu")

print("rotate-center sekil kontrolu: " + ("OK" if not failures else "HATALI"))
for f in failures:
    print("FAIL: " + f)
raise SystemExit(1 if failures else 0)
