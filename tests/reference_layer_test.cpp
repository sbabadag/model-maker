// REFERANS KATMANI EKRAN-UZAYI YARDIMCILARI — portatif test (OCC'siz, Linux'ta kosar).
//
// Kullanici sarti: "zoom in arttiginda zoom cok yavasliyor bunu hizlandiralim
// olduca". Sabit referans katmani (yapi gridi akslari + UCS eksen glifi) DUNYA
// biriminde tanimlidir; kirpma/olcek kurallari olmadan zoom arttikca AYNI
// geometri ekranda dogrusal buyur ve kare maliyeti sisar.
//
// Bu test IKI sey yapar:
//   1. Yardimcilari dogrudan sinar (kirpma geometrisi + glif ekran-olcegi).
//   2. GERCEK mm::Camera + GERCEK gridAxesForView ile derin zoom taramasi
//      yapar ve cizilecek toplam uzunlugun SINIRLI kaldigini olcer. Tesadufen
//      sinirlilik degil: kirpma kaldirilirsa ham toplam kat kat buyur, bu
//      testte ham deger de olculur ki regresyon sinyali kaybolmasin.

#include "model_maker/camera.hpp"
#include "model_maker/grid.hpp"
#include "model_maker/reference_layer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

namespace {

int failures = 0;

void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

// --- 1) Kirpma geometrisi ---------------------------------------------------
void clipBasics() {
    {
        double x0 = 10, y0 = 20, x1 = 30, y1 = 40;
        check(clipSegmentToRect(x0, y0, x1, y1, 0, 0, 100, 100) && x0 == 10 && y0 == 20 &&
                  x1 == 30 && y1 == 40,
              "tamamen icerde segment DEGISMEDEN doner");
    }
    {
        double x0 = -50, y0 = 50, x1 = 150, y1 = 50;
        const bool ok = clipSegmentToRect(x0, y0, x1, y1, 0, 0, 100, 100);
        check(ok && std::abs(x0 - 0.0) < 1e-9 && std::abs(x1 - 100.0) < 1e-9 && y0 == 50 && y1 == 50,
              "yatay segment sol/sag kenara kirpilir");
    }
    {
        double x0 = -10, y0 = -10, x1 = -5, y1 = -5;
        check(!clipSegmentToRect(x0, y0, x1, y1, 0, 0, 100, 100),
              "tamamen disaridaki segment gorunmez (false)");
    }
    {
        // Segmente PARALEL kenar: x sabit ve disarida.
        double x0 = 200, y0 = 0, x1 = 200, y1 = 100;
        check(!clipSegmentToRect(x0, y0, x1, y1, 0, 0, 100, 100),
              "paralel ve disaridaki dikey segment gorunmez");
    }
    {
        double x0 = 50, y0 = 50, x1 = 50, y1 = 50;
        check(clipSegmentToRect(x0, y0, x1, y1, 0, 0, 100, 100) && x0 == 50 && x1 == 50,
              "sifir uzunlukta icerdeki segment gecerli sayilir");
    }
    {
        // Dev segment (derin zoom'da tipik): kirpilmis uzunluk kosegeni asmaz.
        double x0 = -1.0e6, y0 = 300.0, x1 = 1.0e6, y1 = 700.0;
        const bool ok = clipSegmentToRect(x0, y0, x1, y1, -64, -64, 1664, 964);
        const double len = std::hypot(x1 - x0, y1 - y0);
        check(ok && len <= 2.0 * std::hypot(1728.0, 1028.0),
              "DEV segment kirpilinca uzunluk tuval olcegine iner");
        check(ok && std::abs(x0 - (-64.0)) < 1e-9 && std::abs(x1 - 1664.0) < 1e-9,
              "dev segment kirpma dikdortgeninin kenarlarina oturur");
    }
}

// --- 2) Glif ekran olcegi ---------------------------------------------------
void glyphStaysScreenSized() {
    for (double px : {0.01, 0.05, 0.5, 1.0, 5.0, 12.63, 100.0, 1000.0}) {
        const double worldLen = referenceGlyphWorldLength(px, 120.0);
        const double screenLen = worldLen * px;
        check(std::abs(screenLen - 120.0) < 1e-6 * 120.0,
              "px/unit=" + std::to_string(px) + " -> glif ekranda 120 px");
    }
    // ESKI (bozuk) formul kelepceye oturup zoom ile buyurdu; regresyon sinyali.
    const double deepPx = 12.63; // 48 detent ~ 231x
    const double oldWorldLen = std::clamp(120.0 / deepPx, 60.0, 60000.0);
    const double oldScreenLen = oldWorldLen * deepPx;
    check(oldScreenLen > 700.0,
          "ESKI dunya-birimi kelepcesi derin zoom'da ekranda 758 px verirdi (regresyon)");
    check(std::abs(referenceGlyphWorldLength(0.0, 120.0) - 2.0) < 1e-12,
          "dejenere olcekte (px=0) guvenli sabit uzunluk");
}

// --- 3) GERCEK kamera + grid: derin zoom taramasi ---------------------------
constexpr int kW = 1600;
constexpr int kH = 900;

GridDefinition makeGrid() {
    GridDefinition grid;
    grid.xCoords = {0.0, 6000.0, 12000.0, 18000.0};
    grid.yCoords = {0.0, 5000.0, 10000.0, 15000.0};
    grid.zCoords = {0.0, 3600.0, 7200.0};
    rebuildGridAxes(grid);
    return grid;
}

struct Totals {
    double raw{};
    double clipped{};
    int segments{};
};

Totals measure(const Camera& cam, const GridDefinition& grid) {
    Totals totals;
    // Tuval + 64 px pay (renderer'in kirpma dikdortgeni ile ayni).
    const double margin = 64.0;
    for (const auto& axis : gridAxesForView(grid, GridViewPlane::XY)) {
        const Vec2 a = cam.project(axis.from, kW, kH);
        const Vec2 b = cam.project(axis.to, kW, kH);
        totals.raw += std::hypot(b.x - a.x, b.y - a.y);
        double x0 = a.x, y0 = a.y, x1 = b.x, y1 = b.y;
        if (!clipSegmentToRect(x0, y0, x1, y1, -margin, -margin, kW + margin, kH + margin))
            continue;
        const double len = std::hypot(x1 - x0, y1 - y0);
        if (len < 0.5) continue;
        totals.clipped += len;
        ++totals.segments;
    }
    return totals;
}

void deepZoomDrawsABoundedAmount() {
    const GridDefinition grid = makeGrid();
    const Vec3 lo{0.0, 0.0, 0.0};
    const Vec3 hi{18000.0, 15000.0, 7200.0};
    const double bound = 22000.0; // 8 aks x ~1836 px kosegen + pay

    double rawAtDeepest = 0.0;
    double clippedAtDeepest = 0.0;
    for (int detents = 0; detents <= 48; detents += 4) {
        Camera cam;
        cam.fit3D(lo, hi, kW, kH);
        // Tekerlek zoom'u imlec altindaki noktaya kilitli; grid orijinini sec.
        const Vec2 anchor = cam.project({0.0, 0.0, 0.0}, kW, kH);
        for (int i = 0; i < detents; ++i) cam.zoom3DAt(anchor, 1.12, kW, kH);
        const Totals totals = measure(cam, grid);
        check(totals.clipped <= bound,
              "detent " + std::to_string(detents) + ": kirpilmis toplam " +
                  std::to_string(static_cast<long>(totals.clipped)) + " px <= sinir");
        if (detents == 48) {
            rawAtDeepest = totals.raw;
            clippedAtDeepest = totals.clipped;
        }
    }
    check(rawAtDeepest > 200000.0,
          "derin zoom'da HAM toplam buyuk (" +
              std::to_string(static_cast<long>(rawAtDeepest)) + " px)");
    check(clippedAtDeepest > 0.0 && rawAtDeepest > 20.0 * clippedAtDeepest,
          "kirpma ham toplami kat kat azaltir (ham " +
              std::to_string(static_cast<long>(rawAtDeepest)) + " px -> cizilen " +
              std::to_string(static_cast<long>(clippedAtDeepest)) + " px)");
}

} // namespace

int main() {
    clipBasics();
    glyphStaysScreenSized();
    deepZoomDrawsABoundedAmount();
    std::printf("\nreference layer: %s (%d hata)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
