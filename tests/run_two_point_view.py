#!/usr/bin/env python3
"""Run the real Application two-point-view picking methods against a fake Win32 boundary.

startTwoPointViewCommand / startWorkPlaneCommand / cancelWorkPlaneCommand /
commitWorkPlanePoint are extracted VERBATIM from src/application.cpp and linked
against the real core (viewFromTwoPoints, WorkPlane). Does NOT exercise Qt MDI.
Usage: python3 tests/run_two_point_view.py [build-directory]
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
app = (root / "src/application.cpp").read_text(encoding="utf-8")


def method(name):
    m = re.search(r"^void Application::" + name + r"\(.*?^}", app, re.M | re.S)
    assert m, f"{name} not found"
    return m.group()


shell = r'''
#include "model_maker/view_definition.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
constexpr int MB_ICONWARNING = 0;
static int g_beeps = 0;
void MessageBeep(int) { ++g_beeps; }
namespace mm {
enum class EditMode { Draw2D, View3D };
enum class TransformCommand { None, Move };
// Konstrüksiyon düzlemi (P0) varsayılan boyutu — commitWorkPlanePoint'in
// ConstructionPlane dalı bu sabiti kullanır (uygulama ile aynı değer).
constexpr double kDefaultConstructionPlaneSize = 500.0;
// Sahte kamera: yalnizca donus merkezi (R) dalinin cagrisini karsilar —
// kamera davranisi bu dosyada test EDILMEZ (bkz. tests/orbit_pivot_test.cpp).
struct Camera {
    void setOrbitPivot(const Vec3&) {}
};
// Sahte belge: ConstructionPlane dalinin document_.pushSnapshot /
// addConstructionPlane cagrisini karsilar (gercek Document davranisi
// tests/construction_objects_test.cpp'de ayri dogrulanir).
struct FakeDocument {
    int constructionPlanes = 0;
    void pushSnapshot() {}
    std::size_t addConstructionPlane(const WorkPlane&, double) { ++constructionPlanes; return 0; }
};
class Application {
public:
    enum class PointPickPurpose { WorkPlane, TwoPointView, TwoPointViewDirection, ThreePointView, RotateCenter, ConstructionPlane };
    PointPickPurpose pointPickPurpose_{PointPickPurpose::WorkPlane};
    // Donus merkezi (R) yolu bu dosyada test EDILMEZ: commitWorkPlanePoint'in
    // RotateCenter dali camera_.setOrbitPivot cagirir. Sahte kamera yeter.
    Camera camera_;
    std::function<void(const ViewDefinition&)> twoPointViewCallback_;
    bool workPlanePicking_{false};
    std::vector<Vec3> workPlanePoints_;
    // Yon secim asamasinda secilen bakis yonu (birim). updateHover burada no-op
    // oldugu icin varsayilan -Y kalir; yon dali testte dogrudan yazilarak
    // egzersiz edilir.
    Vec3 twoPointViewNormal_{0.0, -1.0, 0.0};
    WorkPlane workPlane_{WorkPlane::world()};
    EditMode mode_{EditMode::Draw2D};
    bool drawingActive_{true};
    TransformCommand transformCommand_{TransformCommand::None};
    FakeDocument document_;
    std::optional<int> hover_;
    POINT_PLACEHOLDER
    std::wstring lastStatus;
    void cancelZoomWindow2D() {}
    void cancelTransformCommand() { transformCommand_ = TransformCommand::None; }
    void cancelDrawing() {}
    void updateHover(int, int) {}
    void updateControls() {}
    void updateStatus() {}
    void invalidateCanvas() {}
    void clearTemporaryTracking() {}
    void publishStatus(const std::wstring& s) { lastStatus = s; }
    void pushUndoSnapshot() { document_.pushSnapshot(); }
    void startWorkPlaneCommand();
    void startTwoPointViewCommand();
    void startThreePointViewCommand();
    void cancelWorkPlaneCommand();
    void commitWorkPlanePoint(const Vec3& point);
};
'''.replace("POINT_PLACEHOLDER", "struct { int x{}, y{}; } cursorScreen_;")
for name in ["startWorkPlaneCommand", "startTwoPointViewCommand", "startThreePointViewCommand",
             "cancelWorkPlaneCommand", "commitWorkPlanePoint"]:
    shell += method(name) + "\n"
shell += "}\n" + r'''
static int checks = 0, failures = 0;
static void check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; } }
int main() {
    using namespace mm;
    { // 1) Normal: iki nokta -> YON secimi -> ucuncu tik -> callback tek kez
        Application a; int calls = 0; ViewDefinition got;
        a.twoPointViewCallback_ = [&](const ViewDefinition& v) { ++calls; got = v; };
        a.startTwoPointViewCommand();
        check(a.workPlanePicking_ && a.mode_ == EditMode::View3D, "command enters 3D picking");
        a.commitWorkPlanePoint({0, 0, 0});
        check(calls == 0 && a.workPlanePoints_.size() == 1, "first point stored, no view yet");
        a.commitWorkPlanePoint({6000, 0, 0});
        check(calls == 0 && a.workPlanePicking_, "second point enters direction phase, no view yet");
        check(a.pointPickPurpose_ == Application::PointPickPurpose::TwoPointViewDirection,
              "purpose = direction selection");
        // Varsayilan normal -Y (P1->P2 = +X icin sagdan bakis); yon tiklamasi onaylar.
        a.commitWorkPlanePoint({0, -1, 0});
        check(calls == 1, "direction click opens exactly one view");
        check(std::abs(got.right.x - 1.0) < 1e-12 && std::abs(got.up.z - 1.0) < 1e-12, "view basis P1->P2 / Z up");
        check(std::abs(got.normal.y + 1.0) < 1e-12, "normal = -Y (default side)");
        check(!a.workPlanePicking_ && a.workPlanePoints_.empty(), "command finished");
        check(a.pointPickPurpose_ == Application::PointPickPurpose::WorkPlane, "purpose reset");
        check(a.workPlane_.normal.z == 1.0, "work plane untouched by two-point view");
    }
    { // 2) Ayni XY (dusey) ikinci nokta reddedilir, komut surer, sonra gecerli nokta calisir
        Application a; int calls = 0;
        a.twoPointViewCallback_ = [&](const ViewDefinition&) { ++calls; };
        a.startTwoPointViewCommand();
        a.commitWorkPlanePoint({100, 200, 0});
        const int beeps = g_beeps;
        a.commitWorkPlanePoint({100, 200, 3000});
        check(calls == 0 && g_beeps == beeps + 1, "vertical pair rejected with beep");
        check(a.workPlanePicking_ && a.workPlanePoints_.size() == 1, "still waiting for 2nd point");
        check(!a.lastStatus.empty(), "user told why");
        a.commitWorkPlanePoint({100, 5200, 0});
        check(calls == 0 && a.workPlanePicking_, "valid 2nd point enters direction phase");
        a.commitWorkPlanePoint({100, 5201, 0});
        check(calls == 1, "direction click after rejection opens view");
    }
    { // 2b) YON secimi: normal diger taraf secilirse gorunum aynalanir
        Application a; int calls = 0; ViewDefinition got;
        a.twoPointViewCallback_ = [&](const ViewDefinition& v) { ++calls; got = v; };
        a.startTwoPointViewCommand();
        a.commitWorkPlanePoint({0, 0, 0});
        a.commitWorkPlanePoint({6000, 0, 0});
        // Imlec cizginin +Y tarafinda -> normal +Y olmali; right terslenmeli (ayna).
        a.twoPointViewNormal_ = {0.0, 1.0, 0.0};
        a.commitWorkPlanePoint({0, 1, 0});
        check(calls == 1, "flipped direction opens view");
        check(std::abs(got.normal.y - 1.0) < 1e-12, "normal = +Y (flipped)");
        check(got.right.x < -0.99, "right mirrored (normal = right x up preserved)");
    }
    { // 3) NaN nokta reddedilir (cokme yok)
        Application a; int calls = 0;
        a.twoPointViewCallback_ = [&](const ViewDefinition&) { ++calls; };
        a.startTwoPointViewCommand();
        a.commitWorkPlanePoint({std::nan(""), 0, 0});
        check(a.workPlanePoints_.empty() && calls == 0, "NaN point ignored");
    }
    { // 4) Iptal (Esc/sag tik) -> callback yok, amac sifirlanir; sonraki duzlem komutu 3 nokta ister
        Application a; int calls = 0;
        a.twoPointViewCallback_ = [&](const ViewDefinition&) { ++calls; };
        a.startTwoPointViewCommand();
        a.commitWorkPlanePoint({0, 0, 0});
        a.cancelWorkPlaneCommand();
        check(calls == 0 && !a.workPlanePicking_, "cancel opens nothing");
        a.startWorkPlaneCommand();
        a.commitWorkPlanePoint({0, 0, 0});
        a.commitWorkPlanePoint({1000, 0, 0});
        check(calls == 0 && a.workPlanePicking_, "work plane command still needs 3 points");
        a.commitWorkPlanePoint({0, 0, 1000});
        check(!a.workPlanePicking_ && std::abs(a.workPlane_.normal.y) > 0.99, "work plane from 3 points still works");
    }
    { // 5) Callback yoksa (Win32 kabugu) cokme yok
        Application a;
        a.startTwoPointViewCommand();
        a.commitWorkPlanePoint({0, 0, 0});
        a.commitWorkPlanePoint({1, 1, 0});
        check(a.workPlanePicking_ && a.pointPickPurpose_ == Application::PointPickPurpose::TwoPointViewDirection,
              "no callback -> still in direction phase (safe)");
        a.commitWorkPlanePoint({0, -1, 0});
        check(!a.workPlanePicking_, "no callback -> command ends safely");
    }
    { // 6) 3 NOKTALI GORUNUS: origin + X + Y -> tek view, dogru taban
        Application a; int calls = 0; ViewDefinition got;
        a.twoPointViewCallback_ = [&](const ViewDefinition& v) { ++calls; got = v; };
        a.startThreePointViewCommand();
        check(a.workPlanePicking_ && a.pointPickPurpose_ == Application::PointPickPurpose::ThreePointView,
              "3-pt command enters picking");
        a.commitWorkPlanePoint({0, 0, 0});
        a.commitWorkPlanePoint({1000, 0, 0});
        check(calls == 0 && a.workPlanePicking_, "3-pt: 2 nokta, henuz view yok");
        a.commitWorkPlanePoint({0, 1000, 0});
        check(calls == 1, "3-pt: ucuncu nokta view acar");
        check(std::abs(got.normal.z - 1.0) < 1e-9, "3-pt: XY duzlemi -> normal +Z (plan)");
        check(!a.workPlanePicking_, "3-pt: komut bitti");
        // Dogrusal noktalar reddedilir (X=Y=0 ayni dogru).
        Application b; int calls2 = 0;
        b.twoPointViewCallback_ = [&](const ViewDefinition&) { ++calls2; };
        b.startThreePointViewCommand();
        b.commitWorkPlanePoint({0, 0, 0});
        b.commitWorkPlanePoint({1000, 0, 0});
        b.commitWorkPlanePoint({2000, 0, 0});
        check(calls2 == 0 && b.workPlanePicking_, "3-pt: dogrusal noktalar reddedilir");
    }
    { // 7) KONSTRUKSIYON DUZLEMI (P0): 3 nokta -> document'e duzlem eklenir,
      // is-duzlemi DEGISMEZ, komut biter. Dogrusal noktalar reddedilir.
        Application a;
        a.pointPickPurpose_ = Application::PointPickPurpose::ConstructionPlane;
        a.workPlanePicking_ = true;
        a.commitWorkPlanePoint({0, 0, 0});
        a.commitWorkPlanePoint({1000, 0, 0});
        check(a.document_.constructionPlanes == 0 && a.workPlanePicking_, "CP: 2 nokta, henuz duzlem yok");
        a.commitWorkPlanePoint({0, 0, 1000});
        check(a.document_.constructionPlanes == 1, "CP: 3. nokta duzlem ekler");
        check(!a.workPlanePicking_, "CP: komut bitti");
        check(a.workPlane_.normal.z == 1.0, "CP: is-duzlemi (work plane) DEGISMEZ");
        check(a.pointPickPurpose_ == Application::PointPickPurpose::WorkPlane, "CP: amac sifirlanir");
        // Dogrusal noktalar reddedilir (duzlem kurulamaz, belge degismez).
        Application b;
        b.pointPickPurpose_ = Application::PointPickPurpose::ConstructionPlane;
        b.workPlanePicking_ = true;
        b.commitWorkPlanePoint({0, 0, 0});
        b.commitWorkPlanePoint({1000, 0, 0});
        b.commitWorkPlanePoint({2000, 0, 0});
        check(b.document_.constructionPlanes == 0 && b.workPlanePicking_, "CP: dogrusal noktalar reddedilir");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
'''
build.mkdir(parents=True, exist_ok=True)
generated = build / "two_point_view_generated.cpp"
binary = build / "two_point_view_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                "-I", str(root / "include"), str(generated), str(build / "libmodel_maker_core.a"),
                "-o", str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
