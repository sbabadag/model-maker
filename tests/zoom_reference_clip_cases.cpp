// Template for run_zoom_reference_clip.py: production code replaces @GRID_BLOCK@.
//
// DERIN ZOOM'DA REFERANS KATMANI MALIYETI. Kullanici: "zoom in arttiginda zoom
// cok yavasliyor bunu hizlandiralim oldukca". Uretimdeki grid cizim blogu
// renderer.cpp'den VERBATIM cikarilir; bu kez GERCEK `mm::Camera` (uretim
// sinifi) + gercek `gridAxesForView` ile derlenir ve KAYIT TUTAN sahte GDI
// cizilen her segmentin EKRAN uzunlugunu toplar.
//
// Iki invaryant sinanir:
//   (1) cizilen toplam ekran uzunlugu zoom'dan BAGIMSIZ kalmali (kirpma),
//   (2) ekranda GORUNEN hicbir aks dusurulmemeli (kirpma cizimi kaybettirmesin).
// Gorunurluk, uretim kirpmasindan BAGIMSIZ olarak ornekleme ile olculur.
//
// Bu native Windows GUI / GDI rasterizasyon testi DEGILDIR.
#include "model_maker/camera.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/grid.hpp"
#include "model_maker/reference_layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
using mm::clipSegmentToRect;
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
    dc->calls.push_back({"line", dc->cursor, {x, y}, {}});
    dc->cursor = {x, y};
}
void drawText(HDC dc, int x, int y, const wchar_t* text, COLORREF) {
    const std::wstring wide(text);
    dc->calls.push_back({"text", {x, y}, {}, std::string(wide.begin(), wide.end())});
}

// --- GERCEK uretim kamerasi -------------------------------------------------
int g_width = 1600;
int g_height = 900;
mm::Camera camera;
POINT projectPoint(Vec3 p) {
    const Vec2 s = camera.project(p, g_width, g_height);
    return POINT{static_cast<long>(std::lround(s.x)), static_cast<long>(std::lround(s.y))};
}
struct Document {
    std::vector<mm::GridDefinition> gridList;
    const std::vector<mm::GridDefinition>& grids() const { return gridList; }
};

struct Drawn {
    int segments{};
    double totalLength{};
    double shortest{1e18};
    int labels{};
};

Drawn render(const Document& document) {
    DC context;
    HDC targetDc = &context;
    RECT canvas{0, 0, g_width, g_height};
    (void)canvas;
// @GRID_BLOCK@
    require(context.pen == stockPen, "GDI pen not restored");
    require(liveObjects == 0, "GDI object leak");
    Drawn drawn;
    for (const auto& c : context.calls) {
        if (c.kind == "line") {
            const double len = std::hypot(static_cast<double>(c.b.x - c.a.x),
                                          static_cast<double>(c.b.y - c.a.y));
            ++drawn.segments;
            drawn.totalLength += len;
            drawn.shortest = std::min(drawn.shortest, len);
        } else if (c.kind == "text") {
            ++drawn.labels;
        }
    }
    return drawn;
}

// --- Bagimsiz gorunurluk olcumu (uretim kirpmasindan BAGIMSIZ: ornekleme) ----
bool segmentCrossesRect(const Vec3& a, const Vec3& b, double margin, int samples = 20000) {
    const Vec2 pa = camera.project(a, g_width, g_height);
    const Vec2 pb = camera.project(b, g_width, g_height);
    for (int i = 0; i <= samples; ++i) {
        const double t = static_cast<double>(i) / samples;
        const double x = pa.x + (pb.x - pa.x) * t;
        const double y = pa.y + (pb.y - pa.y) * t;
        if (x >= -margin && x <= g_width + margin && y >= -margin && y <= g_height + margin)
            return true;
    }
    return false;
}

mm::GridDefinition makeGrid() {
    mm::GridDefinition grid;
    grid.xCoords = {0.0, 6000.0, 12000.0, 18000.0};
    grid.yCoords = {0.0, 5000.0, 10000.0, 15000.0};
    grid.zCoords = {0.0, 3600.0, 7200.0};
    grid.xLabels = {"A", "B", "C", "D"};
    grid.yLabels = {"1", "2", "3", "4"};
    grid.zLabels = {"+0", "+3600", "+7200"};
    mm::rebuildGridAxes(grid);
    return grid;
}

Document scene() {
    Document document;
    document.gridList.push_back(makeGrid());
    return document;
}

void setUpCamera(int detents, const Vec3& zoomCenter) {
    camera = mm::Camera{};
    camera.fit3D(Vec3{0.0, 0.0, 0.0}, Vec3{18000.0, 15000.0, 7200.0}, g_width, g_height);
    if (detents <= 0) return;
    // Tekerlek zoom'u IMLEC ALTINDAKI noktaya kilitlidir (uygulamada
    // zoom3DAt(cursor)). Merkez, ekrandaki zoom noktasidir.
    const Vec2 anchor = camera.project(zoomCenter, g_width, g_height);
    for (int i = 0; i < detents; ++i)
        camera.zoom3DAt(anchor, 1.12, g_width, g_height);
}

// Gecerli akslarin HAM (kirpilmamis) toplam uzunlugu + gorunen aks sayisi.
struct RawInfo {
    double rawTotal{};        // TUM akslarin ham ekran uzunlugu (gorunmese de)
    double rawVisibleTotal{}; // yalniz TUVALDE GORUNEN akslarin ham uzunlugu
    int visibleAxes{};        // kati tuval [0,W]x[0,H] ile kesisen akslar
    int marginAxes{};         // uretim kirpma dikdortgeni (tuval + 64 px pay) ile kesisenler
    int totalAxes{};
};
RawInfo rawInfo(const Document& document) {
    RawInfo info;
    const GridViewPlane plane = gridViewPlaneForDirection(camera.gridViewDirection());
    for (const auto& grid : document.grids()) {
        if (!grid.visible) continue;
        for (const auto& axis : gridAxesForView(grid, plane)) {
            ++info.totalAxes;
            const Vec2 a = camera.project(axis.from, g_width, g_height);
            const Vec2 b = camera.project(axis.to, g_width, g_height);
            const double len = std::hypot(b.x - a.x, b.y - a.y);
            info.rawTotal += len;
            if (segmentCrossesRect(axis.from, axis.to, 0.0)) {
                ++info.visibleAxes;
                info.rawVisibleTotal += len;
            }
            if (segmentCrossesRect(axis.from, axis.to, 64.0)) ++info.marginAxes;
        }
    }
    return info;
}

// (1) Kirpma cizgi uzunlugunu tuval boyutuna baglar: zoom arttikca maliyet
// SABIT kalmali. Sinir: her cizgi <= kosegen + pay; 8 aks icin genis tavan.
//
// Zoom merkezi GRID ORIJINI (0,0,0): gercek kullanimda kullanici imleci bir aks
// uzerine getirip zoom yapar; o akslar (origin'den gecenler) gorunur kalir ve
// HAM uzunluklari zoom ile patlar. Model merkezine zoom yapilirsa derin
// zoom'da hicbir aks gorunmez (grid cizgileri goruste degildir) — sinayacak
// bir sey kalmaz.
void clipBoundsTheDrawnLength() {
    const Document document = scene();
    const Vec3 gridOrigin{0.0, 0.0, 0.0};
    for (int detents : {0, 4, 8, 16, 24, 32, 40, 48}) {
        setUpCamera(detents, gridOrigin);
        const Drawn drawn = render(document);
        const double bound = 22000.0; // 8 aks x ~1836 px kosegen + pay
        require(drawn.totalLength <= bound,
                "detent " + std::to_string(detents) + ": cizilen toplam uzunluk " +
                    std::to_string(drawn.totalLength) + " px siniri asti");
    }
    // Derin zoom'da: gorunen akslarin HAM uzunlugu cizilenin KAT KAT uzerinde
    // olmali (kirpma gercekten is yapiyor) ve yine de cizilen sinirli kalmali.
    setUpCamera(48, gridOrigin);
    const Drawn deep = render(document);
    const RawInfo raw = rawInfo(document);
    require(raw.visibleAxes >= 2,
            "grid orijinine zoom'da origin akslari gorunmeli, gorunen: " +
                std::to_string(raw.visibleAxes));
    require(raw.rawVisibleTotal > 20.0 * deep.totalLength,
            "kirpma etkisiz gorunuyor: gorunen ham " +
                std::to_string(raw.rawVisibleTotal) + " px vs cizilen " +
                std::to_string(deep.totalLength) + " px");
    require(raw.rawVisibleTotal > 200000.0,
            "gorunen ham uzunluk beklenen buyukluge ulasmadi: " +
                std::to_string(raw.rawVisibleTotal));
}

// (2) KIRPMA CIZIMI KAYBETTIRMEMELI ama fazladan da cizmemeli:
//   * tuvalde gorunen HER aks cizilmis olmali  (drawn >= visible),
//   * kirpma dikdortgeni (tuval + 64 px pay) DISINDA bir sey cizilmemeli
//     (drawn <= marginAxes) — yani pay bandi disindaki dev akslar gercekten
//     kirpiliyor.
// Iki zoom merkezi (model merkezi + grid orijini) farkli gorunurluk desenleri
// uretir; ikisinde de gecerli olmali.
void clippingNeverDropsAVisibleAxis() {
    const Document document = scene();
    const std::vector<Vec3> centers = {Vec3{9000.0, 7500.0, 3600.0}, Vec3{0.0, 0.0, 0.0}};
    for (const auto& center : centers) {
        for (int detents : {0, 4, 8, 16, 24, 32, 40, 48}) {
            setUpCamera(detents, center);
            const Drawn drawn = render(document);
            const RawInfo raw = rawInfo(document);
            require(drawn.segments >= raw.visibleAxes,
                    "detent " + std::to_string(detents) + ": gorunen " +
                        std::to_string(raw.visibleAxes) + " aks ama cizilen " +
                        std::to_string(drawn.segments) + " (gorunen aks DUSTU)");
            require(drawn.segments <= raw.marginAxes,
                    "detent " + std::to_string(detents) + ": cizilen " +
                        std::to_string(drawn.segments) + " aks, kirpma dikdortgeninde " +
                        std::to_string(raw.marginAxes) + " (pay disinda cizim)");
        }
    }
}

// (3) Regresyon sinyali: kirpma GERCEKTEN is yapiyor mu? En derin zoom'da
// gorunen akslarin ham uzunlugu cizilenin kat kat uzerinde olmali. (Kirpma
// kaldirilirsa Drawn toplam ham degere cikar ve sinir testi duser.)
void unclippedLengthWouldBeFarLarger() {
    const Document document = scene();
    setUpCamera(48, Vec3{0.0, 0.0, 0.0});
    const Drawn drawn = render(document);
    const RawInfo raw = rawInfo(document);
    require(raw.rawVisibleTotal > 20.0 * drawn.totalLength,
            "kirpma etkisiz gorunuyor: gorunen ham " +
                std::to_string(raw.rawVisibleTotal) + " px vs cizilen " +
                std::to_string(drawn.totalLength) + " px");
    require(raw.rawTotal > 500000.0,
            "ham uzunluk beklenen buyukluge ulasmadi: " + std::to_string(raw.rawTotal));
}

// (4) Normal zoom'da hicbir aks kaybolmamali ve kisacik/dejenere cizgi olmamali.
void normalZoomDrawsEveryAxis() {
    const Document document = scene();
    setUpCamera(0, Vec3{9000.0, 7500.0, 3600.0});
    const Drawn drawn = render(document);
    require(drawn.segments == 8, "fit zoom'da 8 aks beklenirdi, alinan: " +
                                     std::to_string(drawn.segments));
    require(drawn.shortest >= 0.5, "dejenere (0 uzunlukta) cizgi uretildi");
    require(drawn.labels == 8, "fit zoom'da 8 aks etiketi beklenirdi, alinan: " +
                                   std::to_string(drawn.labels));
}

int main() {
    int passed = 0, failed = 0;
    const std::vector<std::pair<std::string, void (*)()>> tests = {
        {"clipping bounds the drawn length across zoom", clipBoundsTheDrawnLength},
        {"clipping never drops a visible axis", clippingNeverDropsAVisibleAxis},
        {"unclipped length would be far larger", unclippedLengthWouldBeFarLarger},
        {"normal zoom draws every axis", normalZoomDrawsEveryAxis},
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
    std::cout << "Zoom reference-layer clipping: " << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
