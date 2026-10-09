#!/usr/bin/env python3
"""Run the real Application::applyViewSelection against a fake Win32 boundary.

The method body is extracted VERBATIM from src/application.cpp; Document is the real
core. Does NOT exercise Qt mouse delivery (TwoPointViewWidget) on Windows.
Usage: python3 tests/run_view_selection.py [build-directory]
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
app = (root / "src/application.cpp").read_text(encoding="utf-8")
m = re.search(r"^bool Application::applyViewSelection\(.*?^}", app, re.M | re.S)
assert m, "applyViewSelection not found"

shell = r'''
#include "model_maker/document.hpp"
#include <algorithm>
#include <iostream>
#include <optional>
#include <vector>
constexpr int MB_ICONWARNING = 0;
static int g_beeps = 0;
void MessageBeep(int) { ++g_beeps; }
namespace mm {
enum class TransformCommand { None, Move, Offset, Delete };
enum class TransformPhase { Selecting, BasePoint, Destination };
enum class GumballHandle { None = -1, AxisX = 0 };
struct Pt { int x{}, y{}; };
class Application {
public:
    enum class ViewSelectOp { Toggle, Add, Clear };
    Document document_;
    std::vector<std::size_t> selectedModels_;
    TransformCommand transformCommand_{TransformCommand::None};
    TransformPhase transformPhase_{TransformPhase::Selecting};
    bool workPlanePicking_{}, zoomWindowActive_{}, drawingActive_{};
    std::optional<int> profileGrip_;
    GumballHandle gumballDrag_{GumballHandle::None};
    std::optional<Pt> selectionFirstCorner_;
    Pt cursorScreen_{};
    int redraws = 0, controls = 0, drawCancels = 0;
    void cancelDrawing() { ++drawCancels; drawingActive_ = false; selectedModels_.clear(); selectionFirstCorner_.reset(); }
    void updateHover(int, int) {}
    void updateControls() { ++controls; }
    void invalidateCanvas() { ++redraws; }
    bool applyViewSelection(ViewSelectOp op, const std::vector<std::size_t>& indices);
};
''' + m.group() + r'''
}
static int checks = 0, failures = 0;
static void check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; } }
using V = std::vector<std::size_t>;
static void seed(mm::Application& a) {
    for (int i = 0; i < 4; ++i) a.document_.addModel(mm::WireframeModel::line({0, double(i), 0}, {1, double(i), 0}));
}
int main() {
    using namespace mm;
    using Op = Application::ViewSelectOp;
    { Application a; seed(a);
      check(a.applyViewSelection(Op::Toggle, {2}) && a.selectedModels_ == V{2}, "click selects");
      a.applyViewSelection(Op::Toggle, {0});
      check(a.selectedModels_ == V({2, 0}), "second click adds (keeps order)");
      a.applyViewSelection(Op::Toggle, {2});
      check(a.selectedModels_ == V{0}, "click on selected deselects");
      a.applyViewSelection(Op::Add, {0, 1, 3});
      check(a.selectedModels_ == V({0, 1, 3}), "window adds without duplicates");
      check(a.controls > 0 && a.redraws > 0, "main window refreshed (status/props/gumball)");
      a.applyViewSelection(Op::Clear, {});
      check(a.selectedModels_.empty(), "Esc / empty click clears");
    }
    { Application a; seed(a);
      a.applyViewSelection(Op::Add, {1, 99, static_cast<std::size_t>(-1)});
      check(a.selectedModels_ == V{1}, "out-of-range indices ignored (no crash)");
      a.applyViewSelection(Op::Toggle, {77});
      check(a.selectedModels_ == V{1}, "invalid toggle ignored");
    }
    { Application a; seed(a);  // Move komutu secim fazinda -> secim komuta gider
      a.transformCommand_ = TransformCommand::Move;
      check(a.applyViewSelection(Op::Add, {0, 2}) && a.selectedModels_ == V({0, 2}),
            "selection feeds a command in Selecting phase");
    }
    { Application a; seed(a);  // Move hedef fazi -> secim degismez
      a.transformCommand_ = TransformCommand::Move; a.transformPhase_ = TransformPhase::Destination;
      a.selectedModels_ = {1}; const int beeps = g_beeps;
      check(!a.applyViewSelection(Op::Toggle, {2}), "rejected while command waits for a point");
      check(a.selectedModels_ == V{1} && g_beeps == beeps + 1, "selection untouched + beep");
    }
    { Application a; seed(a);  // Offset tek nesne
      a.transformCommand_ = TransformCommand::Offset;
      a.applyViewSelection(Op::Add, {0, 3});
      check(a.selectedModels_ == V{3}, "offset keeps exactly one object");
    }
    { Application a; seed(a);  // cizim araci acik -> notr secime gecer
      a.drawingActive_ = true;
      a.applyViewSelection(Op::Toggle, {1});
      check(!a.drawingActive_ && a.drawCancels == 1 && a.selectedModels_ == V{1},
            "active draw tool cancelled, then object selected");
    }
    { Application a; seed(a);  // ana penceredeki yarim secim kutusu temizlenir
      a.selectionFirstCorner_ = Pt{5, 5};
      a.applyViewSelection(Op::Toggle, {0});
      check(!a.selectionFirstCorner_, "pending main-window rubber band reset");
    }
    for (int k = 0; k < 3; ++k) { Application a; seed(a);  // modal durumlar reddedilir
      if (k == 0) a.workPlanePicking_ = true;
      if (k == 1) a.gumballDrag_ = GumballHandle::AxisX;
      if (k == 2) a.profileGrip_ = 1;
      check(!a.applyViewSelection(Op::Toggle, {0}) && a.selectedModels_.empty(),
            "rejected during point picking / gumball drag / grip move");
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
'''
build.mkdir(parents=True, exist_ok=True)
generated = build / "view_selection_generated.cpp"
binary = build / "view_selection_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                "-I", str(root / "include"), str(generated), str(build / "libmodel_maker_core.a"),
                "-o", str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
