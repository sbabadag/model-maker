// Flicker mekanizmasi: sabit referans katmani (UCS glifi + hayalet duzlem)
// ekran-olcegini YUVARLANMIS/TRASLANMIS projeksiyondan turetiyor.
// renderer.cpp projectPoint: static_cast<LONG>(x) = TRUNCATION (yuvarlama degil).
// glif uzunlugu  = 120 / glyphPx        (glyphPx truncated)
// ghost yaricapi = 0.30*span / ghostPx  (ghostPx truncated)
// Saf zoom'da bu nicelikler DUZ ve MONOTON olmali; traslama testere disi uretir.
#include "model_maker/camera.hpp"

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

using namespace mm;

struct P2 { double x, y; };

int main() {
    const int W = 800, H = 600;
    const double span = static_cast<double>(W < H ? W : H); // canvas kisa kenari

    Camera camera;           // 3B, varsayilan
    WorkPlane plane = WorkPlane::world();

    const auto projectPrecise = [&](const Vec3& p) -> P2 {
        const Vec2 s = camera.project(p, W, H);
        return {s.x, s.y};
    };
    // renderer.cpp'deki projectPoint ile BIREBIR: cast LONG = kesme (pozitifte taban)
    const auto projectTrunc = [&](const Vec3& p) -> P2 {
        const Vec2 s = camera.project(p, W, H);
        return {std::trunc(s.x), std::trunc(s.y)};
    };
    const auto projectRound = [&](const Vec3& p) -> P2 {
        const Vec2 s = camera.project(p, W, H);
        return {std::round(s.x), std::round(s.y)};
    };

    const Vec3 origin = plane.origin;
    const Vec3 unitU  = plane.fromPlane({1.0, 0.0});

    std::vector<double> glyphTrunc, glyphPrecise;   // cizilen ok uzunlugu (px)
    std::vector<double> ghostTrunc, ghostPrecise, ghostRound; // cizilen yarim kenar (px)

    camera.zoomBy(1.0); // no-op; net olsun
    for (int step = 0; step < 240; ++step) {
        const double z = std::pow(1.004, static_cast<double>(step));
        // kamerayi bilinen zoom'a getir (reset + zoomBy)
        Camera c2;
        c2.zoomBy(z);
        camera = c2;

        // --- UCS glifi ---
        const P2 oT = projectTrunc(origin), uT = projectTrunc(unitU);
        const P2 oP = projectPrecise(origin), uP = projectPrecise(unitU);
        const double glyphPxT = std::hypot(uT.x - oT.x, uT.y - oT.y);
        const double glyphPxP = std::hypot(uP.x - oP.x, uP.y - oP.y);
        const double lenT = glyphPxT > 1e-9 ? std::clamp(120.0 / glyphPxT, 60.0, 60000.0) : 2.0;
        const double lenP = glyphPxP > 1e-9 ? std::clamp(120.0 / glyphPxP, 60.0, 60000.0) : 2.0;
        // CIZILEN ok uzunlugu: dunya uzunlugu * gercek olcek (kesme yok)
        glyphTrunc.push_back(glyphPxP * lenT);
        glyphPrecise.push_back(glyphPxP * lenP);

        // --- hayalet duzlem ---
        const double spanPxT = std::hypot(uT.x - oT.x, uT.y - oT.y);
        const double spanPxP = std::hypot(uP.x - oP.x, uP.y - oP.y);
        const double halfT = spanPxT > 1e-9 ? std::clamp(0.30 * span / spanPxT, 1e-3, 1e7) : 1.0;
        const double halfP = spanPxP > 1e-9 ? std::clamp(0.30 * span / spanPxP, 1e-3, 1e7) : 1.0;
        ghostTrunc.push_back(spanPxP * halfT);
        ghostPrecise.push_back(spanPxP * halfP);
        const P2 oR = projectRound(origin), uR = projectRound(unitU);
        const double spanPxR = std::hypot(uR.x - oR.x, uR.y - oR.y);
        const double halfR = spanPxR > 1e-9 ? std::clamp(0.30 * span / spanPxR, 1e-3, 1e7) : 1.0;
        ghostRound.push_back(spanPxP * halfR);
    }

    const auto report = [&](const char* name, const std::vector<double>& v) {
        double maxStep = 0.0, minV = 1e18, maxV = -1e18;
        int signFlips = 0, prevSign = 0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            minV = std::min(minV, v[i]);
            maxV = std::max(maxV, v[i]);
            if (i) {
                const double d = v[i] - v[i - 1];
                maxStep = std::max(maxStep, std::fabs(d));
                const int s = (d > 1e-9) ? 1 : (d < -1e-9 ? -1 : 0);
                if (s != 0 && prevSign != 0 && s != prevSign) ++signFlips;
                if (s != 0) prevSign = s;
            }
        }
        std::printf("%-22s max kare-adi=%.3f px   aralik=[%.2f, %.2f]   yon-degisimi(on-off jitter)=%d\n",
                    name, maxStep, minV, maxV, signFlips);
    };

    std::printf("== Saf zoom taramasi (240 kare, zoom 1.00 -> 2.61) ==\n");
    std::printf("-- UCS ok uzunlugu (ideal: sabit 120 px, puruzsuz) --\n");
    report("TRUNCATION (mevcut)", glyphTrunc);
    report("PRECISE (onerilen)", glyphPrecise);
    std::printf("-- Hayalet duzlem yarim-kenar (ideal: puruzsuz buyume) --\n");
    report("TRUNCATION (eski)", ghostTrunc);
    report("ROUNDING (yeni cizim)", ghostRound);
    report("PRECISE (duzeltme)", ghostPrecise);
    return 0;
}
