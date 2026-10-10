#!/usr/bin/env python3
"""Run the real multi-viewport methods of Application against a fake Win32 boundary.

ensureViewRegistry / viewIndexOf / swapViewFields / applyPickFilter /
activateViewport / routeCanvasMessage / syncPassiveViewports /
invalidateOtherViewports / removeViewport / destroyViewport / createViewport /
viewportDefinition / setViewportDepth / fitViewport / setStandardView
are extracted VERBATIM from
src/application.cpp and linked with the real core (Document, Camera, view slab).
handleCanvasMessage, Renderer, CreateWindowExW are fakes that RECORD what the real
code asked for. Does NOT exercise real Win32 message delivery or Qt.
Usage: python3 tests/run_multi_viewport.py [build-directory]
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
app = (root / "src/application.cpp").read_text(encoding="utf-8")


def method(sig):
    m = re.search(r"^" + re.escape(sig) + r"\(.*?^}", app, re.M | re.S)
    assert m, f"{sig} not found"
    return m.group()


methods = [
    "LRESULT CALLBACK Application::canvasProc",
    "void Application::ensureViewRegistry",
    "std::optional<std::size_t> Application::viewIndexOf",
    "void Application::swapViewFields",
    "void Application::applyPickFilter",
    "void Application::activateViewport",
    "LRESULT Application::routeCanvasMessage",
    "void Application::syncPassiveViewports",
    "void Application::invalidateOtherViewports",
    "HWND Application::createViewport",
    "void Application::removeViewport",
    "void Application::destroyViewport",
    "std::optional<ViewDefinition> Application::viewportDefinition",
    "void Application::setViewportDepth",
    "void Application::fitViewport",
    "void Application::setStandardView",
]

shell = r'''
#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/view_definition.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
// ---- sahte Win32 siniri --------------------------------------------------
using HWND = void*; using UINT = unsigned; using WPARAM = unsigned long long;
using LPARAM = long long; using LRESULT = long long;
struct RECT { long left{}, top{}, right{}, bottom{}; };
constexpr UINT WM_PAINT = 0x000F, WM_ERASEBKGND = 0x0014, WM_SIZE = 0x0005, WM_SETCURSOR = 0x0020,
    WM_NCHITTEST = 0x0084, WM_NCDESTROY = 0x0082, WM_CAPTURECHANGED = 0x0215, WM_DESTROY = 0x0002,
    WM_MOUSEFIRST = 0x0200, WM_MOUSEMOVE = 0x0200, WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202,
    WM_MOUSELAST = 0x020E, WM_MOUSEWHEEL = 0x020A, WM_KEYFIRST = 0x0100, WM_KEYDOWN = 0x0100,
    WM_CHAR = 0x0102, WM_KEYLAST = 0x0109, WM_SETFOCUS = 0x0007, WM_TIMER = 0x0113;
constexpr int WS_CHILD = 1, WS_VISIBLE = 2, WS_TABSTOP = 4, WS_CLIPSIBLINGS = 8, GWLP_USERDATA = -21;
static std::set<HWND> g_alive; static std::map<HWND, int> g_invalidated; static int g_nextHwnd = 100;
static HWND g_capture = nullptr, g_focus = nullptr; static int g_releaseCapture = 0;
static bool g_failCreate = false;
static std::map<HWND, long long> g_userData;
using LONG_PTR = long long;
constexpr bool FALSE = false;
inline unsigned LOWORD(LPARAM v) { return static_cast<unsigned>(v & 0xFFFF); }
inline unsigned HIWORD(LPARAM v) { return static_cast<unsigned>((v >> 16) & 0xFFFF); }
static const wchar_t canvasClassName[] = L"ModelMakerCanvas";
static void* instance_ = nullptr;
bool IsWindow(HWND h) { return h && g_alive.count(h); }
bool GetClientRect(HWND h, RECT* r) { *r = RECT{0, 0, IsWindow(h) ? 800 : 0, IsWindow(h) ? 600 : 0}; return IsWindow(h); }
bool InvalidateRect(HWND h, const void*, bool) { ++g_invalidated[h]; return true; }
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM) { return 0; }
HWND GetCapture() { return g_capture; }
bool ReleaseCapture() { ++g_releaseCapture; g_capture = nullptr; return true; }
HWND GetFocus() { return g_focus; }
HWND SetFocus(HWND h) { HWND o = g_focus; g_focus = h; return o; }
long long SetWindowLongPtrW(HWND h, int, long long v);
bool DestroyWindow(HWND h) { g_alive.erase(h); return true; }
long long SetWindowLongPtrW(HWND h, int, long long v) { g_userData[h] = v; return 0; }
#define CALLBACK
constexpr UINT WM_NCCREATE = 0x0081;
struct CREATESTRUCTW { void* lpCreateParams; };
using WNDPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
static WNDPROC g_canvasProc = nullptr;
long long GetWindowLongPtrW(HWND h, int) { return g_userData.count(h) ? g_userData[h] : 0; }
HWND CreateWindowExW(int, const wchar_t*, const wchar_t*, int, int, int, long, long, HWND parent, void*, void*, void* param) {
    if (g_failCreate || !IsWindow(parent)) return nullptr;
    HWND h = reinterpret_cast<HWND>(static_cast<long long>(g_nextHwnd++)); g_alive.insert(h);
    // Gercek Win32 gibi: olusturma sirasinda WM_NCCREATE pencere yordamina gider.
    CREATESTRUCTW cs{param};
    if (g_canvasProc) g_canvasProc(h, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&cs));
    return h; }
namespace mm {
enum class EditMode { Draw2D, View3D };
enum class VisualStyle { Wireframe, Solid };
enum class GumballHandle { None = -1, AxisX = 0 };
enum class TransformCommand { None, Move, Copy, Offset, Mirror, Delete };
struct Renderer { int id{}; };
struct FakeBackend { void resize(unsigned, unsigned) {} };
struct POINT { long x{}, y{}; };
class Application {
public:
    struct ViewportState {
        HWND canvas{};
        Camera camera;
        EditMode mode{EditMode::View3D};
        std::unique_ptr<Renderer> renderer;
        WorkPlane workPlane{};
        std::optional<ViewDefinition> viewDef;
    };
    // gorunuse ozgu (aktif) alanlar
    HWND canvas_{};
    Camera camera_;
    EditMode mode_{EditMode::Draw2D};
    std::unique_ptr<Renderer> renderer_{std::make_unique<Renderer>()};
    std::unique_ptr<FakeBackend> renderBackend_;
    WorkPlane workPlane_{};
    std::optional<ViewDefinition> viewDef_;
    // ortak durum
    Document document_;
    std::vector<std::size_t> selectedModels_;
    VisualStyle visualStyle_{VisualStyle::Solid};
    std::vector<std::unique_ptr<ViewportState>> views_;
    std::size_t activeView_{0};
    bool creatingViewportCanvas_{false};
    std::uint64_t passiveSignature_{~0ull};
    bool rotating_{}, panning2D_{}, viewCubeManipulating_{}, zoomAnimActive_{}, wheelNavigating_{}, snapPreviewActive_{};
    double zoomAnimTarget_{1.0}, wheelPreviewFactor_{1.0};
    Vec2 wheelPreviewOffset_{};
    std::optional<StandardView> viewCubePressedView_;
    std::optional<POINT> selectionFirstCorner_, zoomWindowFirstCorner_;
    std::optional<int> hover_;
    GumballHandle gumballDrag_{GumballHandle::None};
    int transformPhaseMarker{7}; // komut durumu (ortak) — gecis bozmamali
    // kayit
    std::vector<std::pair<HWND, UINT>> handled; std::vector<std::size_t> painted;
    int gumballCancels = 0, controls = 0;
    int nextIndexToAdd = 0; // handler'in belgeye model eklemesi icin
    bool handlerAdds = false;
    LRESULT handleCanvasMessage(UINT message, WPARAM, LPARAM) {
        handled.push_back({canvas_, message});
        if (handlerAdds && message == WM_LBUTTONUP)
            document_.addModel(WireframeModel::line({0, 0, 0}, {1, 0, 0}));
        return 42;
    }
    void paintPassiveViewport(std::size_t index) { painted.push_back(index); }
    void gumballCancelDrag() { ++gumballCancels; gumballDrag_ = GumballHandle::None; }
    void updateControls() { ++controls; }
    void invalidateCanvas() { if (canvas_) ++g_invalidated[canvas_]; }
    // Yon kilidi ve komut yan etkileri (gercek govdeler bunlari cagirir).
    bool viewRotationLocked() const noexcept { return viewDef_.has_value(); }
    void cancelZoomWindow2D() {}
    void cancelWorkPlaneCommand() {}
    void cancelTransformCommand() {}
    void cancelDrawing() {}
    void ensureSpaceMouseStarted() {}
    void updateStatus() {}
    void updateHover(int, int) {}
    void zoomExtents2D() { ++zoomExtentsCalls; }
    void publishStatus(const std::wstring&) {}
    void fitViewport(HWND canvas);
    bool drawingActive_{}, workPlanePicking_{};
    std::optional<int> profileGrip_;
    struct { long x{}, y{}; } cursorScreen_{};
    TransformCommand transformCommand_{TransformCommand::None};
    int zoomExtentsCalls = 0;
    void ensureViewRegistry();
    std::optional<std::size_t> viewIndexOf(HWND canvas) const;
    void swapViewFields(ViewportState& slot);
    void applyPickFilter();
    void activateViewport(std::size_t index);
    LRESULT routeCanvasMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void syncPassiveViewports();
    void invalidateOtherViewports();
    HWND createViewport(HWND parent, const ViewDefinition& definition);
    void removeViewport(std::size_t index);
    void destroyViewport(HWND canvas);
    std::optional<ViewDefinition> viewportDefinition(HWND canvas) const;
    void setViewportDepth(HWND canvas, double front, double back);
    void setStandardView(StandardView view);
    static LRESULT CALLBACK canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
};
'''
for sig in methods:
    shell += method(sig) + "\n"
shell += "}\n" + r'''
static int checks = 0, failures = 0;
static void check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; } }
static HWND mkParent() { HWND h = reinterpret_cast<HWND>(static_cast<long long>(g_nextHwnd++)); g_alive.insert(h); return h; }
int main() {
    using namespace mm;
    g_canvasProc = &Application::canvasProc;
    Application a;
    HWND mainCanvas = mkParent(); a.canvas_ = mainCanvas;
    a.mode_ = EditMode::View3D;
    a.camera_.setView(StandardView::Isometric);
    const auto mainR = a.camera_.cameraToWorldMatrix4();
    Renderer* mainRenderer = a.renderer_.get();
    a.document_.addModel(WireframeModel::line({0, 0, 0}, {6000, 0, 0}));       // 0: dilimde
    a.document_.addModel(WireframeModel::line({0, 5000, 0}, {6000, 5000, 0})); // 1: dilim disi
    const auto def = *viewFromTwoPoints({0, 0, 0}, {6000, 0, 0});

    // 1) Viewport olusturma: aktif alanlar DEGISMEZ, yeni tuval kayitli.
    HWND host = mkParent();
    HWND vc = a.createViewport(host, def);
    check(vc != nullptr && IsWindow(vc), "viewport canvas created");
    check(a.canvas_ == mainCanvas && a.activeView_ == 0, "creating a viewport does not steal the active canvas");
    check(a.views_.size() == 2 && a.viewIndexOf(vc) == std::optional<std::size_t>{1}, "viewport registered");
    check(!a.document_.hasPickFilter(), "main view has no pick filter");

    // 2) Ikincil tuvale fare girdisi: o viewport aktif olur, mesaj AYNI isleyiciye gider.
    a.rotating_ = true; a.selectionFirstCorner_ = POINT{1, 1}; a.hover_ = 3;
    LRESULT r = a.routeCanvasMessage(vc, WM_LBUTTONDOWN, 0, 0);
    check(r == 42 && !a.handled.empty() && a.handled.back().first == vc &&
          a.handled.back().second == WM_LBUTTONDOWN, "input on secondary canvas reaches the SAME handler, as that canvas");
    check(a.activeView_ == 1 && a.canvas_ == vc, "secondary viewport became active");
    check(a.viewDef_.has_value() && a.document_.hasPickFilter(), "active secondary view installs its slab pick filter");
    check(a.document_.modelIsPickable(0) && !a.document_.modelIsPickable(1), "filter = this view's slab");
    check(std::abs(a.workPlane_.normal.z) < 1e-12 && std::abs(a.workPlane_.u.x - 1.0) < 1e-12,
          "work plane = vertical view plane (elevation drawing)");
    check(a.mode_ == EditMode::View3D && a.renderer_.get() != mainRenderer, "own mode + own renderer");
    check(!a.rotating_ && !a.selectionFirstCorner_ && !a.hover_ && g_releaseCapture >= 1,
          "screen-space interactions of the old view are reset on switch");
    check(a.transformPhaseMarker == 7, "shared command state survives the switch (e.g. Move half-way)");

    // 2b) Gercek canvasProc uzerinden (USERDATA ile) yonlendirme.
    a.handled.clear();
    Application::canvasProc(vc, WM_KEYDOWN, 0, 0);
    check(!a.handled.empty() && a.handled.back().first == vc, "real canvasProc routes secondary canvas input");

    // 3) Ana tuvale geri: ana kamera/renderer/filtre aynen geri gelir.
    a.routeCanvasMessage(mainCanvas, WM_MOUSEMOVE, 0, 0);
    check(a.activeView_ == 0 && a.canvas_ == mainCanvas && !a.viewDef_, "back to main view");
    check(a.camera_.cameraToWorldMatrix4() == mainR && a.renderer_.get() == mainRenderer,
          "main camera and renderer restored exactly");
    check(!a.document_.hasPickFilter() && a.workPlane_.normal.z == 1.0, "main view: no filter, world work plane");

    // 4) Pasif tuval: WM_PAINT kendi yolundan, isleyiciye GITMEZ, aktiflik degismez.
    a.handled.clear();
    a.routeCanvasMessage(vc, WM_PAINT, 0, 0);
    check(a.handled.empty() && a.painted == std::vector<std::size_t>{1} && a.activeView_ == 0,
          "passive WM_PAINT drawn with its own camera, no activation");
    a.routeCanvasMessage(vc, WM_TIMER, 0, 0);
    check(a.handled.empty() && a.activeView_ == 0, "non-input message on passive canvas does not activate it");

    // 5) Surukleme sirasinda (yakalama ana tuvalde) ikinci tuvalin MOUSEMOVE'u gecis yapmaz.
    g_capture = mainCanvas;
    a.routeCanvasMessage(vc, WM_MOUSEMOVE, 0, 0);
    check(a.activeView_ == 0, "mouse move over other view during a drag does not switch");
    g_capture = nullptr;

    // 6) Belge degisince diger pencere tazelenir; degismezse gereksiz cizim yok.
    a.handlerAdds = true;
    g_invalidated.clear();
    a.routeCanvasMessage(vc, WM_LBUTTONUP, 0, 0); // vc aktif olur, isleyici model ekler
    check(g_invalidated[mainCanvas] >= 1, "document change repaints the other viewport");
    g_invalidated.clear();
    a.handlerAdds = false;
    a.routeCanvasMessage(vc, WM_MOUSEMOVE, 0, 0);
    a.routeCanvasMessage(vc, WM_MOUSEMOVE, 0, 0);
    check(g_invalidated[mainCanvas] == 0, "plain mouse moves do not repaint other views");
    a.selectedModels_ = {0};
    a.syncPassiveViewports(); // Qt kisayolu (Ctrl+Z vb.) yolu
    check(g_invalidated[mainCanvas] == 1, "selection change (from Qt shortcut path) repaints other views");

    // 7) Derinlik: aktif gorunuste filtre aninda guncellenir.
    a.setViewportDepth(vc, 1000, 6000);
    check(a.document_.modelIsPickable(1), "depth change updates the active pick filter");
    check(a.viewportDefinition(vc)->depthBack == 6000.0, "definition reflects new depth");
    a.setViewportDepth(vc, -5, std::nan(""));
    check(a.viewportDefinition(vc)->depthBack == 6000.0 && a.viewportDefinition(vc)->depthFront == 1000.0,
          "invalid depth ignored");

    // 8) Aktif ikincil pencere kapatilir: ana gorunuse guvenle doner, cokme yok.
    HWND vc2 = a.createViewport(host, def);
    a.routeCanvasMessage(vc2, WM_KEYDOWN, 0, 0);
    check(a.activeView_ == 2 && a.canvas_ == vc2, "third viewport active via keyboard");
    a.destroyViewport(vc2);
    check(a.activeView_ == 0 && a.canvas_ == mainCanvas && a.views_.size() == 2 && !IsWindow(vc2),
          "destroying the ACTIVE viewport restores the main view");
    check(a.camera_.cameraToWorldMatrix4() == mainR, "main camera intact after destroy");
    a.routeCanvasMessage(vc2, WM_LBUTTONDOWN, 0, 0); // gec gelen mesaj
    check(a.activeView_ == 0, "late message from destroyed canvas ignored");

    // 9) Pasif pencerenin WM_DESTROY'u (Qt kapatti) kaydi siler; indeksler kayar.
    HWND vc3 = a.createViewport(host, def);
    a.routeCanvasMessage(vc3, WM_LBUTTONDOWN, 0, 0); // vc3 aktif (index 2)
    a.routeCanvasMessage(vc, WM_DESTROY, 0, 0);       // index 1 (pasif) yok oluyor
    check(a.views_.size() == 2 && a.activeView_ == 1 && a.canvas_ == vc3,
          "removing a passive view below the active one keeps the active canvas");
    a.routeCanvasMessage(mainCanvas, WM_LBUTTONDOWN, 0, 0);
    check(a.activeView_ == 0 && a.canvas_ == mainCanvas && !a.document_.hasPickFilter(),
          "main view still restorable after index shift");

    // 10) Ana gorunus kaldirilamaz; gecersiz ebeveyn / basarisiz olusturma guvenli.
    a.destroyViewport(mainCanvas);
    check(a.canvas_ == mainCanvas && IsWindow(mainCanvas), "main view cannot be destroyed via viewport API");
    check(a.createViewport(nullptr, def) == nullptr, "null parent -> no viewport");
    g_failCreate = true;
    const std::size_t before = a.views_.size();
    check(a.createViewport(host, def) == nullptr && a.views_.size() == before && !a.creatingViewportCanvas_,
          "failed CreateWindow leaves registry untouched");
    g_failCreate = false;

    // 11) Gumball suruklenirken pencere degisirse surukleme iptal edilir (yarim transform kalmaz).
    a.gumballDrag_ = GumballHandle::AxisX;
    a.routeCanvasMessage(vc3, WM_LBUTTONDOWN, 0, 0);
    check(a.gumballCancels == 1 && a.gumballDrag_ == GumballHandle::None, "gumball drag cancelled on view switch");

    // 12) YON KILIDI: gorunus penceresinde Ctrl+1..7 / R gorunusu DONDURMEZ
    //     (dilim normali sabit kalsin) — yalnizca iki noktali tabana geri oturtur.
    a.routeCanvasMessage(vc3, WM_LBUTTONDOWN, 0, 0); // vc3 aktif (gorunus)
    check(a.viewRotationLocked(), "view window locks rotation");
    a.camera_.rotate(0.9, 0.4); // gorunusu elle bozalim
    const Vec3 bogusDir = a.camera_.viewTransform({0, 0, 1});
    a.setStandardView(StandardView::Front);
    const Vec3 ex = a.camera_.viewTransform(a.viewDef_->right);
    const Vec3 ez = a.camera_.viewTransform(a.viewDef_->up);
    check(std::abs(ex.x - 1.0) < 1e-9 && std::abs(ex.y) < 1e-9 && std::abs(ex.z) < 1e-9,
          "view window: standard view restores the two-point basis (refit, no rotation)");
    check(std::abs(ez.y - 1.0) < 1e-9 && std::abs(ez.z) < 1e-9,
          "view window: screen-up row is world Z (no roll) after standard view");
    check(a.viewRotationLocked() && a.mode_ == EditMode::View3D && a.viewDef_.has_value(),
          "view window stays a view window (still 3D, same slab)");
    check(a.zoomExtentsCalls == 0, "view window refits its own slab, not the 2D extents path");
    check(std::abs(bogusDir.x - a.camera_.viewTransform({0, 0, 1}).x) > 1e-6,
          "the bogus orientation really was replaced (test would catch a no-op)");

    // 13) ANA GORUNUS: standart gorunus eskisi gibi yonu DEGISTIRIR (davranis korunur).
    a.routeCanvasMessage(mainCanvas, WM_LBUTTONDOWN, 0, 0);
    check(!a.viewRotationLocked(), "main view keeps free rotation");
    const auto mainBefore = a.camera_.cameraToWorldMatrix4();
    a.setStandardView(StandardView::Front);
    check(a.camera_.cameraToWorldMatrix4() != mainBefore, "main view: standard view still changes the view");
    check(a.zoomExtentsCalls == 0, "main view did not fall into the refit path");

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
'''
build.mkdir(parents=True, exist_ok=True)
generated = build / "multi_viewport_generated.cpp"
binary = build / "multi_viewport_test"
generated.write_text(shell, encoding="utf-8")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O0", "-g", "-Wall", "-Wextra",
                "-Wno-unused-parameter", "-fsanitize=address,undefined",
                "-I", str(root / "include"), str(generated), str(build / "libmodel_maker_core.a"),
                "-o", str(binary)], check=True)
raise SystemExit(subprocess.run([str(binary)]).returncode)
