#!/usr/bin/env python3
"""Cizgi/Poliline/Dikdortgen/Daire araclari aktif profili YOK SAYAR (B6).

KULLANICI SARTI (INBOX 2 / B6): panelde aktif profil (orn. HEB240) varken
Cizgi, Polyline, Dikdortgen, Daire, 3DFACE yalniz PROFILSIZ yardimci obje
uretsin; profilli uye yalniz Kolon/Kiris komutlarindan gelsin.

GERCEK Application::commitPoint govdesi, yalniz Win32/Qt siniri sahte
sinirla degistirilerek derlenir (run_gumball_interaction.py ile ayni desen;
metot govdeleri verbatim cikarilir, kopyalanmaz). Windows'a ozel OCC yolu
(assignProfileToLine -> extrude) sahte sinirda KAYDEDILIR — test profil
atamasinin CAGIRILIP CAGIRILMADIGINI ve duz cizginin belgeye girdigini
kanitlar; gercek extrude akip profile_section_reference + ekran dogrulamasi
ile olculur.

Kullanim: python3 tests/run_line_beam_tool.py [build-directory]
"""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
app = (root / "src/application.cpp").read_text(encoding="utf-8")

slice_start = app.index("void Application::commitPoint")
slice_end = app.index("void Application::clearTemporaryTracking")
commit = app[slice_start:slice_end]

shell = r'''
#include "model_maker/drafting.hpp"
#include "model_maker/document.hpp"
#include "model_maker/camera.hpp"
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
#include <unordered_set>
#include <vector>
constexpr int MB_ICONWARNING = 0;
void MessageBeep(int) {}
namespace mm {
enum class EditMode { Draw2D, View3D };
class Application {
public:
    Document document_;
    DrawTool tool_{DrawTool::Line};
    EditMode mode_{EditMode::View3D};
    WorkPlane workPlane_{};
    std::optional<Vec3> anchor_;
    std::vector<Vec3> facePoints_;
    std::optional<std::size_t> polylineModelIndex_;
    std::size_t divideCount_{2};
    std::string pendingProfileName_;
    // Test gozlemleri
    int profileAssignCalls{0};
    std::string lastProfileName;
    std::optional<Vec3> lastProfileFrom, lastProfileTo;
    std::optional<Vec3> lastPlacedColumn;

    void selectTool(DrawTool tool) {
        tool_ = tool;
        anchor_.reset();
        facePoints_.clear();
        polylineModelIndex_.reset();
    }
    void setPendingProfileName(std::string name) { pendingProfileName_ = std::move(name); }
    void pushUndoSnapshot() { document_.pushSnapshot(); }
    void publishStatus(const std::wstring&) {}
    void updateStatus() {}
    void invalidateCanvas() {}
    void updateHover(int, int) {}
    EntityProperties currentEntityProperties() const { return EntityProperties{}; }
    std::optional<Vec3> selectedTwoPointMidpoint() const { return std::nullopt; }
    void addStyledModel(WireframeModel model) { document_.addModel(std::move(model)); }
    void placeColumn(const Vec3& point) { lastPlacedColumn = point; }
    void assignProfileToLine(const Vec3& from, const Vec3& to,
                             const std::string& profileName,
                             std::optional<Vec3> = std::nullopt) {
        ++profileAssignCalls;
        lastProfileName = profileName;
        lastProfileFrom = from;
        lastProfileTo = to;
        document_.addModel(WireframeModel::line(from, to));
    }
    void commitPoint(const Vec3& point);
};
'''
shell += commit + "\n}\n"
shell += (root / "tests/line_beam_tool_cases.cpp").read_text(encoding="utf-8")

build.mkdir(parents=True, exist_ok=True)
generated = build / "line_beam_tool_generated.cpp"
binary = build / "line_beam_tool_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g",
                "-Wall", "-Wextra",
                "-I", str(root / "include"), str(generated),
                str(build / "libmodel_maker_core.a"),
                str(build / "libmm_component_contract.a"),
                str(build / "libmm_foundation.a"),
                "-o", str(binary)], check=True)
subprocess.run([str(binary)], check=True)
