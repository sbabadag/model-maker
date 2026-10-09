#!/usr/bin/env python3
"""Run the real Application gumball methods against the portable core.

Only the Win32/Qt boundary is replaced (capture, repaint, modal callback).
Method bodies and fields are extracted verbatim, never copied/reimplemented.
This does NOT prove native Windows event delivery or Qt dialog rendering.
Usage: python3 tests/run_gumball_interaction.py [build-directory]
"""
from pathlib import Path
import re
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
app = (root / "src/application.cpp").read_text(encoding="utf-8")
header = (root / "include/model_maker/application.hpp").read_text(encoding="utf-8")
renderer = (root / "include/model_maker/renderer.hpp").read_text(encoding="utf-8")
fields = header[header.index("    bool gumballVisible_"):header.index("    std::optional<Vec3> anchor_")]
handles = renderer[renderer.index("enum class GumballHandle"):renderer.index("struct DraftView")]
methods = app[app.index("Vec2 Application::gumballProject"):app.index("DraftView Application::draftView")]
release_match = re.search(r"^void Application::onLeftButtonUp\(.*?^}", app, re.M | re.S)
assert release_match, "onLeftButtonUp extraction marker not found"
release = release_match.group()
shell = r'''
#include "model_maker/document.hpp"
#include "model_maker/camera.hpp"
#include "model_maker/view_cube.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
struct RECT { long left{}, top{}, right{1000}, bottom{800}; };
struct POINT { long x{}, y{}; };
using HWND = int;
static bool captured = false;
void GetClientRect(HWND, RECT* r) { *r = RECT{}; }
void SetCapture(HWND) { captured = true; }
void ReleaseCapture() { captured = false; }
constexpr int MB_ICONWARNING = 0;
void MessageBeep(int) {}
void SetFocus(HWND) {}
namespace mm {
enum class EditMode { Draw2D, View3D };
enum class TransformCommand { None };
'''
shell += handles
shell += r'''
class Application {
public:
    Document document_;
    Camera camera_;
    HWND canvas_{1};
    EditMode mode_{EditMode::View3D};
    TransformCommand transformCommand_{TransformCommand::None};
    bool profileGrip_{}, workPlanePicking_{}, zoomWindowActive_{};
    bool viewCubeManipulating_{}, viewCubeDragged_{}, rotating_{};
    std::optional<StandardView> viewCubePressedView_;
    std::vector<std::size_t> selectedModels_{0};
    std::wstring input_;
    std::function<bool(const std::wstring&, const std::wstring&, std::wstring&)> parameterRequest_;
    void pushUndoSnapshot() { document_.pushSnapshot(); }
    void updateStatus() {}
    void updateControls() {}
    void publishStatus(const std::wstring&) {}
    void invalidateCanvas() {}
    void updateHover(int, int) {}
    void onLeftButtonUp(int x, int y);
'''
shell += fields + "};\n" + methods + release + "\n}\n"
shell += (root / "tests/gumball_interaction_cases.cpp").read_text(encoding="utf-8")
build.mkdir(parents=True, exist_ok=True)
generated = build / "gumball_interaction_generated.cpp"
binary = build / "gumball_interaction_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra", "-I", str(root / "include"), str(generated), str(build / "libmodel_maker_core.a"), "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
