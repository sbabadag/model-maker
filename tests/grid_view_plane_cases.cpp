// Template for run_grid_view_plane.py: production code replaces the @ markers.
//
// Tekla grid gorunus-duzlemi katmanini sinar: uretimdeki grid cizim blogu
// renderer.cpp'den VERBATIM cikarilir, kayit tutan sahte bir GDI sinirina ve
// gercek `gridAxesForView`/`gridViewPlaneForDirection` (src/grid.cpp) karsi
// derlenir. Kamera gorunus yonu sahte kameradan (viewTransform) gelir — yani
// "hangi duzlem" karari GERCEKTEN test edilir.
//
// Bu native Windows GUI / GDI rasterizasyon testi DEGILDIR.
#include "model_maker/geometry.hpp"
#include "model_maker/grid.hpp"
#include "model_maker/reference_layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using mm::Vec2;
using mm::Vec3;
using mm::GridAxisLine;
using mm::GridViewPlane;
using mm::gridAxesForView;
using mm::gridViewPlaneForDirection;
// Uretim kodu `namespace mm` ICINDE oldugu icin niteliksiz cagirir; harness
// global scope'ta oldugundan ayni isimleri buraya getir.
using mm::clipSegmentToRect;
using mm::referenceGlyphWorldLength;
using DWORD = unsigned long;
using COLORREF = std::uint32_t;
constexpr COLORREF RGB(int r, int g, int b) {
    return static_cast<COLORREF>(r) | (static_cast<COLORREF>(g) << 8) | (static_cast<COLORREF>(b) << 16);
}
constexpr int PS_SOLID = 0, PS_GEOMETRIC = 1, PS_USERSTYLE = 2, PS_ENDCAP_FLAT = 3;
constexpr int NULL_PEN = 8, NULL_BRUSH = 5, TRANSPARENT = 1, BS_SOLID = 0;
constexpr int FW_NORMAL = 400, FALSE = 0;
constexpr int DEFAULT_CHARSET = 1, OUT_TT_PRECIS = 4, CLIP_DEFAULT_PRECIS = 0;
constexpr int CLEARTYPE_QUALITY = 5, DEFAULT_PITCH = 0;
struct POINT {
    long x{}, y{};
};
struct RECT {
    long left{}, top{}, right{}, bottom{};
};
struct LOGBRUSH {
    unsigned style{};
    COLORREF color{};
    unsigned hatch{};
};
enum class Kind { Pen, Brush, Font };
struct Object {
    Kind kind{Kind::Pen};
    int width{};
    COLORREF color{};
    bool stock{};
};
using HGDIOBJ = Object*;
using HPEN = Object*;
using HBRUSH = Object*;
using HFONT = Object*;
struct Call {
    std::string kind;
    POINT a{}, b{};
    int width{};
    COLORREF color{};
    std::string text;
};
Object stockPenObject{Kind::Pen, 1, 0, true};
Object stockBrushObject{Kind::Brush, 0, 0, true};
Object stockFontObject{Kind::Font, 0, 0, true};
Object* const stockPen = &stockPenObject;
Object* const stockBrush = &stockBrushObject;
Object* const stockFont = &stockFontObject;
struct DC {
    Object* pen{stockPen};
    Object* brush{stockBrush};
    // Gercek bir DC'de HER ZAMAN bir font secilidir; varsayilani stok font
    // say ki renderer'in "eski fontu geri sec" yolu GERCEKTEN calissin.
    Object* font{stockFont};
    POINT cursor{};
    std::vector<Call> calls;
};
using HDC = DC*;
int liveObjects = 0;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
HPEN CreatePen(int, int width, COLORREF color) {
    ++liveObjects;
    return new Object{Kind::Pen, width, color};
}
HPEN ExtCreatePen(int, int width, const LOGBRUSH* brush, int, const DWORD*) {
    ++liveObjects;
    return new Object{Kind::Pen, width, brush ? brush->color : 0};
}
HBRUSH CreateSolidBrush(COLORREF color) {
    ++liveObjects;
    return new Object{Kind::Brush, 0, color};
}
HFONT CreateFontW(int, int, int, int, int, int, int, int, int, int, int, int, int, const wchar_t*) {
    ++liveObjects;
    return new Object{Kind::Font};
}
HGDIOBJ GetStockObject(int id) {
    require(id == NULL_PEN || id == NULL_BRUSH, "unexpected stock object");
    return id == NULL_PEN ? static_cast<HGDIOBJ>(&stockPenObject) : static_cast<HGDIOBJ>(&stockBrushObject);
}
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object) {
    Object*& slot = object->kind == Kind::Brush
                        ? dc->brush
                        : (object->kind == Kind::Font ? dc->font : dc->pen);
    Object* const old = slot;
    slot = object;
    return old;
}
void DeleteObject(HGDIOBJ object) {
    require(!object->stock, "stock object deleted");
    --liveObjects;
    delete object;
}
int SetBkMode(HDC, int) { return 0; }
void MoveToEx(HDC dc, int x, int y, POINT*) { dc->cursor = {x, y}; }
void LineTo(HDC dc, int x, int y) {
    dc->calls.push_back({"line", dc->cursor, {x, y}, dc->pen->width, dc->pen->color, {}});
    dc->cursor = {x, y};
}
void drawText(HDC dc, int x, int y, const wchar_t* text, COLORREF color) {
    const std::wstring wide(text);
    dc->calls.push_back({"text", {x, y}, {}, 0, color, std::string(wide.begin(), wide.end())});
}
// @LINE_FUNCTION@

// Sahte kamera. Renderer gorunus duzlemini KILITLI bakis yonunden
// (gridViewDirection) alir — CANLI rotasyondan (R) DEGIL. `lockedPlaneWins*`
// testi ikisini bilerek AYIRIR: R baska bir yone donuktur, kilit plan kalir.
struct Camera {
    double R[3][3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vec3 dir{0.0, 0.0, 1.0}; // Camera::gridViewDirection() kilitli dunya bakis yonu
    Vec3 viewTransform(Vec3 v) const {
        return Vec3{R[0][0] * v.x + R[0][1] * v.y + R[0][2] * v.z,
                    R[1][0] * v.x + R[1][1] * v.y + R[1][2] * v.z,
                    R[2][0] * v.x + R[2][1] * v.y + R[2][2] * v.z};
    }
    const Vec3& gridViewDirection() const { return dir; }
};
struct Document {
    std::vector<mm::GridDefinition> gridList;
    const std::vector<mm::GridDefinition>& grids() const { return gridList; }
};

// Duzgun (afin) projeksiyon: dunya X -> ekran (65,0), Y -> (-24,-24),
// Z -> (0,65). Uc yon AYRIK oldugu icin cizilen segmentlerin ailesi
// projeksiyondan bagimsiz olarak siniflandirilabilir.
//
// OLCEK: tuval 800 px ve grid (6000x5000 mm + 500 uzanti) SIGMALI. Kirpma
// devrede oldugu icin (bkz. reference_layer.hpp) ekran disina TASAN akslar
// artik cizilmez; 65 px/mm ile gridin tamami ekranin yuz binlerce piksel
// disina duser ve aile sayimi bozulurdu. 0.06 px/mm ile tum grid [0,800]
// icine oturur. Yon deseni (65,-24,0) AYNEN korunur; kScreen* yalnizca YON
// tasir (axisOf normalize eder), bu yuzden olcekten bagimsizdir.
double g_zoom = 1.0;
constexpr double kGridPxPerUnit = 0.06;
POINT projectPoint(Vec3 p) {
    const double yShear = 24.0 / 65.0;
    return {std::lround(400.0 + g_zoom * kGridPxPerUnit * (p.x - yShear * p.y)),
            std::lround(300.0 + g_zoom * kGridPxPerUnit * (p.z - yShear * p.y))};
}
const POINT kScreenX{65, 0};
const POINT kScreenY{-24, -24};
const POINT kScreenZ{0, 65};

std::vector<Call> render(const Camera& camera, const Document& document, int span) {
    DC context;
    HDC targetDc = &context;
    RECT canvas{0, 0, span, span};
    (void)canvas;
// @GRID_BLOCK@
    require(context.pen == stockPen && context.brush == stockBrush, "GDI selections not restored");
    require(context.font == stockFont, "GDI font selection not restored");
    require(liveObjects == 0, "GDI object leak");
    return context.calls;
}

mm::GridDefinition makeGrid() {
    mm::GridDefinition grid;
    grid.name = "GRID";
    grid.xCoords = {0.0, 6000.0};
    grid.yCoords = {0.0, 5000.0};
    grid.zCoords = {0.0, 3600.0};
    grid.xLabels = {"1", "2"};
    grid.yLabels = {"A", "B"};
    grid.zLabels = {"+0", "+3600"};
    grid.extendLeft = 500.0;
    grid.extendRight = 500.0;
    grid.extendBelow = 500.0;
    grid.extendAbove = 500.0;
    mm::rebuildGridAxes(grid);
    return grid;
}

// Cizilen segmentin hangi dunya eksenine paralel oldugu (projeksiyondan).
char axisOf(const Call& call) {
    const double dx = static_cast<double>(call.b.x - call.a.x);
    const double dy = static_cast<double>(call.b.y - call.a.y);
    const double len = std::hypot(dx, dy);
    require(len > 1.0, "degenerate grid segment drawn");
    const auto residual = [&](const POINT& dir) {
        const double na = std::hypot(static_cast<double>(dir.x), static_cast<double>(dir.y));
        return std::abs((dx / len) * (dir.y / na) - (dy / len) * (dir.x / na));
    };
    const double rx = residual(kScreenX), ry = residual(kScreenY), rz = residual(kScreenZ);
    require(std::min({rx, ry, rz}) < 0.02, "grid segment is not axis aligned in world space");
    if (rx < ry && rx < rz) return 'X';
    return ry < rz ? 'Y' : 'Z';
}

struct Counts {
    int alongX{};
    int alongY{};
    int alongZ{};
};

std::vector<Call> segments(const std::vector<Call>& calls) {
    std::vector<Call> out;
    for (const auto& c : calls)
        if (c.kind == "line") out.push_back(c);
    return out;
}

Counts classify(const std::vector<Call>& calls) {
    Counts counts;
    for (const auto& c : segments(calls)) {
        const char axis = axisOf(c);
        if (axis == 'X') ++counts.alongX;
        else if (axis == 'Y') ++counts.alongY;
        else ++counts.alongZ;
    }
    return counts;
}

std::vector<std::string> labels(const std::vector<Call>& calls) {
    std::vector<std::string> out;
    for (const auto& c : calls)
        if (c.kind == "text") out.push_back(c.text);
    return out;
}

bool has(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

std::string describe(const Counts& counts) {
    return "X=" + std::to_string(counts.alongX) + " Y=" + std::to_string(counts.alongY) +
           " Z=" + std::to_string(counts.alongZ);
}

// Bakis yonu = e_z (ustten bakis) -> gorunus duzlemi XY.
Camera topCamera() { return Camera{}; }
// Bakis yonu = +e_y -> gorunus duzlemi XZ (on/arka gorunus).
Camera frontCamera() {
    Camera camera;
    camera.R[0][0] = 1; camera.R[0][1] = 0; camera.R[0][2] = 0;
    camera.R[1][0] = 0; camera.R[1][1] = 0; camera.R[1][2] = -1;
    camera.R[2][0] = 0; camera.R[2][1] = 1; camera.R[2][2] = 0;
    camera.dir = Vec3{0.0, 1.0, 0.0};
    return camera;
}
// Bakis yonu = +e_x -> gorunus duzlemi YZ (yan gorunus).
Camera sideCamera() {
    Camera camera;
    camera.R[0][0] = 0; camera.R[0][1] = 0; camera.R[0][2] = 1;
    camera.R[1][0] = 0; camera.R[1][1] = 1; camera.R[1][2] = 0;
    camera.R[2][0] = -1; camera.R[2][1] = 0; camera.R[2][2] = 0;
    camera.dir = Vec3{1.0, 0.0, 0.0};
    return camera;
}
// Izometrik: bakis yonu (1,1,1)/sqrt(3) — uc bilesen ESIT (esitlikte Z kazanir).
Camera isoCamera() {
    const double s2 = std::sqrt(2.0), s6 = std::sqrt(6.0), s3 = std::sqrt(3.0);
    Camera camera;
    camera.R[0][0] = 1 / s2; camera.R[0][1] = -1 / s2; camera.R[0][2] = 0;
    camera.R[1][0] = 1 / s6; camera.R[1][1] = 1 / s6; camera.R[1][2] = -2 / s6;
    camera.R[2][0] = 1 / s3; camera.R[2][1] = 1 / s3; camera.R[2][2] = 1 / s3;
    camera.dir = Vec3{1.0 / s3, 1.0 / s3, 1.0 / s3};
    return camera;
}

void planViewDrawsOnlyTheXyFamilies() {
    Document document;
    document.gridList.push_back(makeGrid());
    const std::size_t baseAxes = document.gridList[0].axes.size();
    const auto calls = render(topCamera(), document, 800);
    const auto counts = classify(calls);
    // X koordinatlari Y'ye paralel, Y koordinatlari X'e paralel cizilir;
    // kat (Z) duzlemleri gorunus duzlemine PARALEL -> cizilmez.
    require(counts.alongY == 2 && counts.alongX == 2 && counts.alongZ == 0,
            "plan gorunusu yalniz X+Y aileleri olmali, alinan: " + describe(counts));
    const auto text = labels(calls);
    require(has(text, "1") && has(text, "2"), "plan: X etiketleri cizilmeli");
    require(has(text, "A") && has(text, "B"), "plan: Y etiketleri cizilmeli");
    require(!has(text, "+0") && !has(text, "+3600"),
            "plan: kat etiketleri cizilmemeli (paralel aile)");
    require(document.gridList[0].axes.size() == baseAxes,
            "cizim taban turevi (snap/zoom) DEGISTIRMEMELI");
}

void frontViewSwitchesToTheXzGrid() {
    Document document;
    document.gridList.push_back(makeGrid());
    const auto counts = classify(render(frontCamera(), document, 800));
    require(counts.alongZ == 2 && counts.alongX == 2 && counts.alongY == 0,
            "on gorunusu XZ gridi vermeli (Y ailesi paralel), alinan: " + describe(counts));
    const auto text = labels(render(frontCamera(), document, 800));
    require(has(text, "1") && has(text, "2"), "on gorunusu: X etiketleri (dikey cizgiler)");
    require(has(text, "+0") && has(text, "+3600"), "on gorunusu: kat etiketleri (yatay cizgiler)");
    require(!has(text, "A") && !has(text, "B"),
            "on gorunusu: Y aileleri/etiketleri cizilmemeli (paralel duzlem)");
}

void sideViewSwitchesToTheYzGrid() {
    Document document;
    document.gridList.push_back(makeGrid());
    const auto counts = classify(render(sideCamera(), document, 800));
    require(counts.alongZ == 2 && counts.alongY == 2 && counts.alongX == 0,
            "yan gorunusu YZ gridi vermeli (X ailesi paralel), alinan: " + describe(counts));
    const auto text = labels(render(sideCamera(), document, 800));
    require(has(text, "A") && has(text, "B"), "yan gorunusu: Y etiketleri (dikey cizgiler)");
    require(has(text, "+0") && has(text, "+3600"), "yan gorunusu: kat etiketleri (yatay cizgiler)");
    require(!has(text, "1") && !has(text, "2"),
            "yan gorunusu: X aileleri/etiketleri cizilmemeli (paralel duzlem)");
}

void isometricViewFallsBackToThePlanGrid() {
    Document document;
    document.gridList.push_back(makeGrid());
    const auto counts = classify(render(isoCamera(), document, 800));
    require(counts.alongY == 2 && counts.alongX == 2 && counts.alongZ == 0,
            "izometrik 3B gorunuste plan gridi (3B grid YOK), alinan: " + describe(counts));
    const auto text = labels(render(isoCamera(), document, 800));
    require(has(text, "1") && has(text, "A"), "izometrik: plan etiketleri cizilmeli");
    require(!has(text, "+0"), "izometrik: kat etiketleri cizilmemeli");
}

void theSameViewAlwaysDrawsTheSameGrid() {
    Document document;
    document.gridList.push_back(makeGrid());
    const auto first = classify(render(frontCamera(), document, 800));
    const auto second = classify(render(frontCamera(), document, 800));
    require(first.alongX == second.alongX && first.alongY == second.alongY &&
                first.alongZ == second.alongZ,
            "ayni gorunus ayni gridi vermeli (deterministik)");
}

void hiddenGridDrawsNothing() {
    Document document;
    mm::GridDefinition grid = makeGrid();
    grid.visible = false;
    document.gridList.push_back(grid);
    require(segments(render(topCamera(), document, 800)).empty(),
            "gorunmez grid hicbir sey cizmemeli");
    require(labels(render(topCamera(), document, 800)).empty(),
            "gorunmez grid etiket cizmemeli");
}

void planOnlyGridKeepsVerticalExtent() {
    // Kat kotu olmayan grid: elevasyonda dikey cizgiler uzantiyla (Below/Above)
    // yine cizilir; yatay kat cizgisi YOK.
    Document document;
    mm::GridDefinition grid;
    grid.xCoords = {0.0, 4000.0};
    grid.yCoords = {0.0, 3000.0};
    grid.extendBelow = 1000.0;
    grid.extendAbove = 1000.0;
    mm::rebuildGridAxes(grid);
    document.gridList.push_back(grid);
    const auto counts = classify(render(frontCamera(), document, 800));
    require(counts.alongZ == 2 && counts.alongX == 0 && counts.alongY == 0,
            "kat yoksa on gorunusunde yalniz X dikey cizgileri, alinan: " + describe(counts));
}

// KILIT (kullanici sarti): gorunus duzlemi CANLI rotasyondan degil, kamerada
// saklanan KILITLI yonden gelir. R front gorunuse donuktur ama kilit plan
// kalir -> plan gridi cizilmeli (donus cizimi degistirmez).
void lockedPlaneWinsOverTheLiveRotation() {
    Document document;
    document.gridList.push_back(makeGrid());
    Camera camera = frontCamera();
    camera.dir = Vec3{0.0, 0.0, 1.0}; // kilit plan (XY); canli R on gorunuste
    const auto counts = classify(render(camera, document, 800));
    require(counts.alongY == 2 && counts.alongX == 2 && counts.alongZ == 0,
            "kilitli plan duzlemi canli rotasyonu ezmeli, alinan: " + describe(counts));
    const auto text = labels(render(camera, document, 800));
    require(has(text, "1") && has(text, "A"), "kilitli plan: X ve Y etiketleri cizilmeli");
    require(!has(text, "+0"), "kilitli plan: kat etiketleri cizilmemeli (paralel aile)");
}

// 3B goruntuyu dondurmek (canli R degisir, KILIT sabit) grid cizimini
// DEGISTIRMEMELI — "goruntunun 3d degismesi grid in cizilimini degistirmez".
void rotatingTheViewKeepsTheSameGridDrawing() {
    Document document;
    document.gridList.push_back(makeGrid());
    Camera camera = isoCamera(); // kilit: (1,1,1) -> plan
    const Counts before = classify(render(camera, document, 800));
    require(before.alongY == 2 && before.alongX == 2 && before.alongZ == 0,
            "izometrik kilit plan vermeli, alinan: " + describe(before));
    // Yorunge donusu: YALNIZ canli R degisir (kamera.rotate ile ayni etki).
    camera.R[0][0] = 0; camera.R[0][1] = 0; camera.R[0][2] = 1;
    camera.R[1][0] = 0; camera.R[1][1] = 1; camera.R[1][2] = 0;
    camera.R[2][0] = -1; camera.R[2][1] = 0; camera.R[2][2] = 0;
    const auto calls = render(camera, document, 800);
    const Counts after = classify(calls);
    require(before.alongX == after.alongX && before.alongY == after.alongY &&
                before.alongZ == after.alongZ && after.alongZ == 0,
            "3B donus grid AILESINI degistirmemeli, alinan: " + describe(after));
    const auto text = labels(calls);
    require(has(text, "1") && has(text, "A") && !has(text, "+0"),
            "donusten sonra da plan etiketleri (kat etiketi YOK)");
}

int main() {
    int passed = 0, failed = 0;
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"plan view draws only the XY families", planViewDrawsOnlyTheXyFamilies},
        {"front view switches to the XZ grid", frontViewSwitchesToTheXzGrid},
        {"side view switches to the YZ grid", sideViewSwitchesToTheYzGrid},
        {"isometric view falls back to the plan grid", isometricViewFallsBackToThePlanGrid},
        {"the same view always draws the same grid", theSameViewAlwaysDrawsTheSameGrid},
        {"a hidden grid draws nothing", hiddenGridDrawsNothing},
        {"a level-less grid keeps its vertical extent", planOnlyGridKeepsVerticalExtent},
        {"the locked plane wins over the live rotation", lockedPlaneWinsOverTheLiveRotation},
        {"rotating the view keeps the same grid drawing", rotatingTheViewKeepsTheSameGridDrawing},
    };
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cout << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << "Grid view plane renderer: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
