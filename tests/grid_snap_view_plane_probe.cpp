// PROBE: iki noktali (XZ/YZ) gorunuste grid snap'i HANGI kotlere gidiyor?
// Gercek SnapEngine + gercek Camera + gercek grid ile olcer. Kopya mantik yok.
//
// SIKAYET (kullanici): "XZ YZ gibi 2 noktayla alinan goruntulerde grid sadece
// xy deki alt grid noktalarina snap oluyor; her grid snap ozelligine
// kilitlenmeli".
#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/grid.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static GridDefinition makeGrid() {
    GridDefinition g;
    g.name = "PROBE";
    g.xCoords = {0.0, 6000.0, 12000.0};
    g.yCoords = {0.0, 5000.0};
    g.zCoords = {0.0, 3600.0, 7200.0};
    rebuildGridAxes(g);
    return g;
}

static SnapTypeMask allOn() {
    SnapTypeMask m{};
    m.fill(true);
    return m;
}

int main() {
    const SnapTypeMask allMask = allOn();
    Document doc;
    doc.addGrid(makeGrid());

    const GridDefinition& g = doc.grids().front();
    std::printf("grid.axes (taban turev) %zu cizgi:\n", g.axes.size());
    for (const auto& a : g.axes)
        std::printf("   from(%.0f,%.0f,%.0f) to(%.0f,%.0f,%.0f) h=%d\n",
                    a.from.x, a.from.y, a.from.z, a.to.x, a.to.y, a.to.z,
                    a.horizontal ? 1 : 0);

    const int W = 1200, H = 800;

    // --- XZ ELEVASYON: ekran saga +X, ekran yukari +Z (bakış Y) ---
    {
        Camera cam;
        cam.setViewBasis(Vec3{1, 0, 0}, Vec3{0, 0, 1}, Vec3{6000, 0, 3600});
        std::printf("\nXZ elevasyon: gridViewDirection=(%.3f,%.3f,%.3f)\n",
                    cam.gridViewDirection().x, cam.gridViewDirection().y,
                    cam.gridViewDirection().z);
        const WorkPlane wp = WorkPlane::fromViewDirection(Vec3{0, 0, 0}, cam.gridViewDirection());

        // (a) x=0 dikey aksi uzerinde, kot 2000 (katlar arasi)
        const Vec3 targetA{0.0, 0.0, 2000.0};
        Vec2 s = cam.project(targetA, W, H);
        SnapResult r = SnapEngine::snap3D(s, doc, cam, W, H, 30.0, wp, true, true,
                                          std::nullopt, &allMask, false);
        std::printf("(a) hedef (0,0,2000)  -> tip=%d nokta=(%.1f,%.1f,%.1f)\n",
                    static_cast<int>(r.type), r.point.x, r.point.y, r.point.z);

        // (b) kat cizgisi z=3600 uzerinde, x=4000
        const Vec3 targetB{4000.0, 0.0, 3600.0};
        s = cam.project(targetB, W, H);
        r = SnapEngine::snap3D(s, doc, cam, W, H, 30.0, wp, true, true,
                               std::nullopt, &allMask, false);
        std::printf("(b) hedef (4000,0,3600) -> tip=%d nokta=(%.1f,%.1f,%.1f)\n",
                    static_cast<int>(r.type), r.point.x, r.point.y, r.point.z);
    }

    // --- YZ ELEVASYON: ekran saga +Y, ekran yukari +Z (bakış X) ---
    {
        Camera cam;
        cam.setViewBasis(Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{0, 2500, 3600});
        std::printf("\nYZ elevasyon: gridViewDirection=(%.3f,%.3f,%.3f)\n",
                    cam.gridViewDirection().x, cam.gridViewDirection().y,
                    cam.gridViewDirection().z);
        const WorkPlane wp = WorkPlane::fromViewDirection(Vec3{0, 0, 0}, cam.gridViewDirection());

        const Vec3 target{0.0, 0.0, 5000.0};   // y=0 dikey aksi uzerinde, kot 5000
        const Vec2 s = cam.project(target, W, H);
        const SnapResult r = SnapEngine::snap3D(s, doc, cam, W, H, 30.0, wp, true, true,
                                                std::nullopt, &allMask, false);
        std::printf("(c) hedef (0,0,5000)   -> tip=%d nokta=(%.1f,%.1f,%.1f)\n",
                    static_cast<int>(r.type), r.point.x, r.point.y, r.point.z);
    }

    std::printf("\nBEKLENEN (duzeltmeden sonra): (a) z=2000, (b) z=3600, (c) z=5000\n");
    std::printf("MEVCUT kod hepsini z=0 (taban) kotuna cekiyorsa sikayet dogrulanir.\n");
    return 0;
}
