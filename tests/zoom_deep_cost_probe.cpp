// DERIN ZOOM'DA REFERANS KATMANI MALIYET OLCUMU (portatif, OCC'siz).
//
// Kullanici: "zoom in arttiginda zoom cok yavasliyor". Suphe: sabit referans
// katmani (UCS oku + yapi gridi) EKRANDA zoom ile BUYUVEN cizgiler uretiyor;
// GDI'da cok uzun cizgi/polyline rasterizasyonu kareyi sisirir.
//
// Formuller renderer.cpp'den BIREBIR:
//   glyphPx  = |project(workPlane.fromPlane({1,0})) - project(workPlane.origin)|
//   glyphLen = clamp(120/glyphPx, 60.0, 60000.0)     <-- DUNYA birimi (alt sinir!)
//   cizilen ekran uzunlugu = glyphLen * glyphPx
//   grid     = gridAxesForView(grid, XY) segmentleri, project ile ekran uzunlugu
//
// Bu probe YALNIZ olcer (assert etmez); zamanlamayi degil, cizilen EKRAN
// UZUNLUGUNU raporlar.

#include "model_maker/camera.hpp"
#include "model_maker/grid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace mm;

namespace {

double screenSegLen(const Camera& cam, const Vec3& a, const Vec3& b, int w, int h) {
    const Vec2 pa = cam.project(a, w, h);
    const Vec2 pb = cam.project(b, w, h);
    return std::hypot(pb.x - pa.x, pb.y - pa.y);
}

} // namespace

int main() {
    const int W = 1600, H = 900;
    const Vec3 lo{0.0, 0.0, 0.0};
    const Vec3 hi{18000.0, 15000.0, 7200.0};

    GridDefinition grid;
    grid.xCoords = {0.0, 6000.0, 12000.0, 18000.0};
    grid.yCoords = {0.0, 5000.0, 10000.0, 15000.0};
    grid.zCoords = {0.0, 3600.0, 7200.0};
    rebuildGridAxes(grid);

    std::printf("detent |  px/unit |  glyph_world |   glyph_px | gridSegs |  gridMaxPx  |  gridSumPx  |  ucsSumPx  |  drawSumPx\n");
    std::printf("-------+----------+--------------+------------+----------+-------------+-------------+------------+------------\n");

    for (int detent = 0; detent <= 48; detent += 4) {
        Camera cam;
        cam.fit3D(lo, hi, W, H);
        for (int i = 0; i < detent; ++i)
            cam.zoom3DAt({W / 2.0, H / 2.0}, 1.12, W, H);

        // --- UCS glifi (renderer formulu) ---
        const WorkPlane wp = WorkPlane::world();
        const Vec2 gOrigin = cam.project(wp.origin, W, H);
        const Vec2 gUnit = cam.project(wp.fromPlane({1.0, 0.0}), W, H);
        const double glyphPx = std::hypot(gUnit.x - gOrigin.x, gUnit.y - gOrigin.y);
        const double glyphLen = glyphPx > 1e-9 ? std::clamp(120.0 / glyphPx, 60.0, 60000.0) : 2.0;
        const double glyphScreen = glyphLen * glyphPx;
        // Her ok: govde 1 cizgi + 2 ok basi ~= govde + 2*~9px
        const double ucsSumPx = 3.0 * (glyphScreen + 18.0);

        // --- Yapi gridi (XY plan ailesi) ---
        const auto axes = gridAxesForView(grid, GridViewPlane::XY);
        double gridMaxPx = 0.0, gridSumPx = 0.0;
        for (const auto& a : axes) {
            const double len = screenSegLen(cam, a.from, a.to, W, H);
            gridMaxPx = std::max(gridMaxPx, len);
            gridSumPx += len;
        }

        std::printf("%6d | %8.2f | %12.1f | %10.1f | %8zu | %11.0f | %11.0f | %10.0f | %10.0f\n",
                    detent, cam.pixelsPerUnit(), glyphLen, glyphScreen, axes.size(),
                    gridMaxPx, gridSumPx, ucsSumPx, gridSumPx + ucsSumPx);
    }

    // --- Animasyon yakinsama: kac kare (15 ms) ve kac ms? ---
    std::printf("\nSoNÜMLEMELI ZOOM (timer 8, 15 ms/kare, step=1+(T-1)*0.28, dur esigi 0.004)\n");
    std::printf("detent  hedef_T   kare   ms\n");
    for (int detents : {1, 2, 4, 8, 16}) {
        double target = 1.0;
        for (int i = 0; i < detents; ++i) target *= 1.12;
        double remaining = target;
        int frames = 0;
        while (std::abs(remaining - 1.0) >= 0.004 && frames < 100000) {
            const bool last = std::abs(remaining - 1.0) < 0.004;
            const double apply = last ? remaining : 1.0 + (remaining - 1.0) * 0.28;
            remaining /= apply;
            ++frames;
        }
        std::printf("%6d  %7.3f  %5d  %5d\n", detents, target, frames, frames * 15);
    }
    return 0;
}
