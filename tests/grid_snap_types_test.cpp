// YAPI GRIDI'nin TUM snap turlerine yapismasi — OCC GEREKTIRMEZ.
//
// KULLANICI SARTI: "ayrica grid e tum snaplar yapismali". Eskiden grid snap
// YALNIZCA iki aile aksinin KESISIMINE (SnapType::Grid) gidiyordu; grid
// cizgileri uzerinde baska hicbir tur (uc/orta/en yakin/dikme) bulunmuyordu.
// Artik `addGridLineCandidates` (src/drafting.cpp) aks cizgilerini model
// kenarlariyla AYNI sekilde aday uretir ve her turun kapisi
// `choose(enabledTypes)` maskesidir.
//
// Bu dosya GERCEK `SnapEngine::snap` / `SnapEngine::snap3D`'yi cagirir —
// kopya mantik yok.
#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/grid.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

// X: {0,6000,12000} dikey akslar, Y: {0,5000} yatay akslar, Z kat yok.
// Uzantilar varsayilan 1000 -> dikey aks x=0: (0,-1000)->(0,6000);
// yatay aks y=0: (-1000,0)->(13000,0). Kesisimler 6 nokta, orijin (0,0).
static GridDefinition makeGrid() {
    GridDefinition g;
    g.name = "TEST";
    g.xCoords = {0.0, 6000.0, 12000.0};
    g.yCoords = {0.0, 5000.0};
    rebuildGridAxes(g);
    return g;
}

static SnapTypeMask maskExcept(std::initializer_list<SnapType> off) {
    SnapTypeMask m{};
    m.fill(true);
    for (const auto type : off) m[static_cast<std::size_t>(type)] = false;
    return m;
}

int main() {
    const double tol = 100.0;   // 2B: dunya birimi
    const SnapTypeMask allOn = maskExcept({});

    // --- 1) DIKEY grid aksinda "en yakin" nokta (basit snap) ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapResult r = SnapEngine::snap({30.0, 1000.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Nearest, "1a grid aksinda Nearest: en yakin nokta snap'i");
        check(near(r.point.x, 0.0) && near(r.point.y, 1000.0),
              "1b grid aksinda Nearest: nokta aks uzerinde (0,1000)");
    }

    // --- 2) MASKE KAPISI: Nearest kapaliysa grid o turu URETMEZ ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapTypeMask noNearest = maskExcept({SnapType::Nearest});
        const SnapResult r = SnapEngine::snap({30.0, 1000.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &noNearest);
        check(r.type == SnapType::Grid, "2a Nearest kapali: o tur uretilmez, eski kesisim kalir");
        check(near(r.point.x, 0.0) && near(r.point.y, 0.0), "2b Nearest kapali: en yakin kesisim (0,0)");
    }

    // --- 3) KESISIM: yapi gridi caprazi SnapType::Grid olarak kalir ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapResult r = SnapEngine::snap({60.0, 70.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Grid, "3a kesisimde: SnapType::Grid (capraz isareti korunur)");
        check(near(r.point.x, 0.0) && near(r.point.y, 0.0), "3b kesisimde: nokta (0,0)");
    }

    // --- 4) UC: aks cizgisinin ucu da snap noktasidir ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapResult r = SnapEngine::snap({40.0, -960.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Endpoint, "4a aks ucunda: Endpoint snap'i");
        check(near(r.point.x, 0.0) && near(r.point.y, -1000.0), "4b aks ucunda: nokta (0,-1000)");
    }

    // --- 5) ORTA NOKTACI: aks cizgisinin ortasi da snap noktasidir ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapResult r = SnapEngine::snap({40.0, 2520.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Midpoint, "5a aks ortasinda: Midpoint snap'i");
        check(near(r.point.x, 0.0) && near(r.point.y, 2500.0), "5b aks ortasinda: nokta (0,2500)");
    }

    // --- 6) DIKME: referans noktasindan aksa dikme ayagi ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapResult r = SnapEngine::snap({30.0, 1010.0, 0.0}, doc, tol, true, true,
                                              std::optional<Vec3>{Vec3{2000.0, 1000.0, 0.0}}, &allOn);
        check(r.type == SnapType::Perpendicular, "6a referansla: Perpendicular snap'i");
        check(near(r.point.x, 0.0) && near(r.point.y, 1000.0), "6b dikme ayagi: (0,1000)");
    }

    // --- 7) GORUNMEZ grid: hicbir tur uretilmez ---
    {
        Document doc;
        GridDefinition g = makeGrid();
        g.visible = false;
        doc.addGrid(g);
        const SnapResult r = SnapEngine::snap({30.0, 1000.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::None, "7a gorunmez grid: snap YOK");
    }

    // --- 8) ESKI DAVRANIS KORUNUR: tolerans disinda kesisime yapisir ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapResult r = SnapEngine::snap({700.0, 700.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Grid, "8a tolerans disi: eski kesisim davranisi korunur");
        check(near(r.point.x, 0.0) && near(r.point.y, 0.0), "8b tolerans disi: en yakin kesisim (0,0)");
    }

    // --- 9) OBJE SNAP'I GRID'I EZER (obje yolu once calisir) ---
    {
        Document doc;
        doc.addGrid(makeGrid());
        doc.addModel(WireframeModel::line({100.0, 1000.0, 0.0}, {500.0, 1000.0, 0.0}));
        const SnapResult r = SnapEngine::snap({280.0, 1005.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Midpoint, "9a obje snap'i kazanir (grid'e kaymaz)");
        check(near(r.point.x, 300.0) && near(r.point.y, 1000.0), "9b obje orta noktasi (300,1000)");
    }

    // --- 10) 3B PLAN GORUNUS: aks uzerinde ekran-uzayi Nearest ---
    {
        const int W = 1200, H = 800;
        Camera topCam;
        topCam.setViewBasis(Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{6000.0, 2500.0, 0.0});
        Document doc; doc.addGrid(makeGrid());
        Vec2 s = topCam.project({0.0, 1000.0, 0.0}, W, H);
        s.x += 25.0;   // aksin 25 px yaninda
        const SnapResult r = SnapEngine::snap3D(s, doc, topCam, W, H, 30.0, WorkPlane::world(),
                                                true, true, std::nullopt, &allOn, false);
        check(r.type == SnapType::Nearest, "10a 3B plan: aks uzerinde Nearest");
        check(std::abs(r.point.x) < 1.0 && std::abs(r.point.y - 1000.0) < 1.0,
              "10b 3B plan: nokta (0,1000)");
    }

    // --- 11) 3B MASKE KAPISI: Nearest kapali -> eski kesisim ---
    {
        const int W = 1200, H = 800;
        Camera topCam;
        topCam.setViewBasis(Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{6000.0, 2500.0, 0.0});
        Document doc; doc.addGrid(makeGrid());
        Vec2 s = topCam.project({0.0, 1000.0, 0.0}, W, H);
        s.x += 25.0;
        const SnapTypeMask noNearest = maskExcept({SnapType::Nearest});
        const SnapResult r = SnapEngine::snap3D(s, doc, topCam, W, H, 30.0, WorkPlane::world(),
                                                true, true, std::nullopt, &noNearest, false);
        check(r.type == SnapType::Grid, "11a 3B Nearest kapali: o tur uretilmez");
    }

    std::printf("\n%d kontrol, %d hata\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
