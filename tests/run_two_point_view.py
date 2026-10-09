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
class Application {
public:
    enum class PointPickPurpose { WorkPlane, TwoPointView };
    PointPickPurpose pointPickPurpose_{PointPickPurpose::WorkPlane};
    std::function<void(const ViewDefinition&)> twoPointViewCallback_;
    bool workPlanePicking_{false};
    std::vector<Vec3> workPlanePoints_;
    WorkPlane workPlane_{WorkPlane::world()};
    EditMode mode_{EditMode::Draw2D};
    bool drawingActive_{true};
    TransformCommand transformCommand_{TransformCommand::None};
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
    void startWorkPlaneCommand();
    void startTwoPointViewCommand();
    void cancelWorkPlaneCommand();
    void commitWorkPlanePoint(const Vec3& point);
};
'''.replace("POINT_PLACEHOLDER", "struct { int x{}, y{}; } cursorScreen_;")
for name in ["startWorkPlaneCommand", "startTwoPointViewCommand", "cancelWorkPlaneCommand",
             "commitWorkPlanePoint"]:
    shell += method(name) + "\n"
shell += "}\n" + r'''
static int checks = 0, failures = 0;
static void check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; } }
int main() {
    using namespace mm;
    { // 1) Normal: iki nokta -> callback tek kez, dogru tanim, komut biter
        Application a; int calls = 0; ViewDefinition got;
        a.twoPointViewCallback_ = [&](const ViewDefinition& v) { ++calls; got = v; };
        a.startTwoPointViewCommand();
        check(a.workPlanePicking_ && a.mode_ == EditMode::View3D, "command enters 3D picking");
        a.commitWorkPlanePoint({0, 0, 0});
        check(calls == 0 && a.workPlanePoints_.size() == 1, "first point stored, no view yet");
        a.commitWorkPlanePoint({6000, 0, 0});
        check(calls == 1, "second point opens exactly one view");
        check(std::abs(got.right.x - 1.0) < 1e-12 && std::abs(got.up.z - 1.0) < 1e-12, "view basis P1->P2 / Z up");
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
        check(calls == 1, "valid 2nd point after rejection opens view");
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
        check(!a.workPlanePicking_, "no callback -> command ends safely");
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
