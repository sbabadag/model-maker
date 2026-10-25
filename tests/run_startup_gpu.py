#!/usr/bin/env python3
"""Run the real Application startup-GPU methods against a fake Win32/backend boundary.

applyStartupDefaults3D / tryEnableStartupGpu / clearStartupGpuGuard / toggleGpuLines
and the guard helpers are extracted VERBATIM from src/application.cpp. Only the
Win32 calls (GetClientRect, GetModuleFileNameW, ...) and the backend factories are
faked. This does NOT prove real driver/WGL behaviour on Windows.
Usage: python3 tests/run_startup_gpu.py [build-directory]
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


helpers_start = app.index("// GL ACILIS KORUMASI")
helpers_end = app.index("} // namespace", helpers_start) + len("} // namespace")
helpers = "namespace {\n" + app[helpers_start:helpers_end]  # source opens it just above the comment

shell = r'''
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
using DWORD = unsigned long;
using HWND = void*;
struct RECT { long left{}, top{}, right{}, bottom{}; };
constexpr int MAX_PATH = 260;
static long g_w = 0, g_h = 0;
static std::wstring g_exe;
static std::wstring g_env;
static int g_glInitCalls = 0;
static bool g_glInitResult = true;
static bool g_glFactoryThrows = false;
bool IsWindow(HWND h) { return h != nullptr; }
bool GetClientRect(HWND, RECT* r) { *r = RECT{0, 0, g_w, g_h}; return true; }
DWORD GetModuleFileNameW(void*, wchar_t* b, DWORD n) {
    std::wcsncpy(b, g_exe.c_str(), n); return static_cast<DWORD>(g_exe.size()); }
DWORD GetEnvironmentVariableW(const wchar_t*, wchar_t* b, DWORD n) {
    if (g_env.empty()) return 0; std::wcsncpy(b, g_env.c_str(), n); return static_cast<DWORD>(g_env.size()); }
namespace mm {
enum class EditMode { Draw2D, View3D };
enum class VisualStyle { Wireframe, Solid, Transparent, HiddenLine };
struct IRenderBackend {
    virtual ~IRenderBackend() = default;
    virtual bool initialize(void*, int, int) = 0;
    virtual void shutdown() = 0;
    virtual bool isHardwareAccelerated() const noexcept = 0;
    virtual void resetDiagnostics() {}
};
struct FakeGl : IRenderBackend {
    bool ok = false;
    bool initialize(void*, int w, int h) override {
        ++g_glInitCalls;
        if (w <= 0 || h <= 0) { std::cerr << "FAIL: GL init on 0x0 canvas\n"; std::exit(2); }
        ok = g_glInitResult; return ok; }
    void shutdown() override { ok = false; }
    bool isHardwareAccelerated() const noexcept override { return ok; }
};
struct FakeGdi : IRenderBackend {
    bool initialize(void*, int, int) override { return true; }
    void shutdown() override {}
    bool isHardwareAccelerated() const noexcept override { return false; }
};
std::unique_ptr<IRenderBackend> createOpenGLRenderBackend() {
    if (g_glFactoryThrows) throw std::bad_alloc();
    return std::make_unique<FakeGl>(); }
std::unique_ptr<IRenderBackend> createGdiRenderBackend() { return std::make_unique<FakeGdi>(); }
'''
shell += helpers + r'''
class Application {
public:
    HWND canvas_{reinterpret_cast<HWND>(1)};
    EditMode mode_{EditMode::Draw2D};
    VisualStyle visualStyle_{VisualStyle::Wireframe};
    std::unique_ptr<IRenderBackend> renderBackend_;
    bool gpuLinesEnabled_ = false;
    bool startupGpuEnabled_ = true;
    bool startupGpuGuardArmed_ = false;
    bool backendInitTried_ = false;
    std::wstring startupNotice_;
    int toggle3DCalls = 0;
    void toggle3DView() { ++toggle3DCalls; mode_ = mode_ == EditMode::View3D ? EditMode::Draw2D : EditMode::View3D; }
    void setVisualStyle(VisualStyle s) noexcept { visualStyle_ = s; }
    void updateStatus() {}
    void invalidateCanvas() {}
    void applyStartupDefaults3D();
    void tryEnableStartupGpu();
    void clearStartupGpuGuard();
    void toggleGpuLines();
};
'''
for name in ["applyStartupDefaults3D", "tryEnableStartupGpu", "clearStartupGpuGuard", "toggleGpuLines"]:
    shell += method(name) + "\n"
shell += "}\n" + (root / "tests/startup_gpu_cases.cpp").read_text(encoding="utf-8")

build.mkdir(parents=True, exist_ok=True)
generated = build / "startup_gpu_generated.cpp"
binary = build / "startup_gpu_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                str(generated), "-o", str(binary)], check=True)
os.environ["MM_STARTUP_TEST_DIR"] = str(build)
raise SystemExit(subprocess.run([str(binary)]).returncode)
