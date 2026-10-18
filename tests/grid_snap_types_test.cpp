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
static bool approx(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

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

// ELEVASYON testleri icin KATLI grid: X/Y plan + Z katlari. Iki noktali
// XZ/YZ gorunusunde duzlem-ici aileler = dikey akslar + KAT cizgileri.
static GridDefinition makeLevelGrid() {
    GridDefinition g;
    g.name = "LEVELS";
    g.xCoords = {0.0, 6000.0, 12000.0};
    g.yCoords = {0.0, 5000.0};
    g.zCoords = {0.0, 3600.0, 7200.0};
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
        check(approx(r.point.x, 0.0) && approx(r.point.y, 1000.0),
              "1b grid aksinda Nearest: nokta aks uzerinde (0,1000)");
    }

    // --- 2) MASKE KAPISI: Nearest kapaliysa grid o turu URETMEZ ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapTypeMask noNearest = maskExcept({SnapType::Nearest});
        const SnapResult r = SnapEngine::snap({30.0, 1000.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &noNearest);
        check(r.type == SnapType::Grid, "2a Nearest kapali: o tur uretilmez, eski kesisim kalir");
        check(approx(r.point.x, 0.0) && approx(r.point.y, 0.0), "2b Nearest kapali: en yakin kesisim (0,0)");
    }

    // --- 3) KESISIM: yapi gridi caprazi SnapType::Grid olarak kalir ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapResult r = SnapEngine::snap({60.0, 70.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Grid, "3a kesisimde: SnapType::Grid (capraz isareti korunur)");
        check(approx(r.point.x, 0.0) && approx(r.point.y, 0.0), "3b kesisimde: nokta (0,0)");
    }

    // --- 4) UC: aks cizgisinin ucu da snap noktasidir ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapResult r = SnapEngine::snap({40.0, -960.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Endpoint, "4a aks ucunda: Endpoint snap'i");
        check(approx(r.point.x, 0.0) && approx(r.point.y, -1000.0), "4b aks ucunda: nokta (0,-1000)");
    }

    // --- 5) ORTA NOKTACI: aks cizgisinin ortasi da snap noktasidir ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapResult r = SnapEngine::snap({40.0, 2520.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Midpoint, "5a aks ortasinda: Midpoint snap'i");
        check(approx(r.point.x, 0.0) && approx(r.point.y, 2500.0), "5b aks ortasinda: nokta (0,2500)");
    }

    // --- 6) DIKME: referans noktasindan aksa dikme ayagi ---
    {
        Document doc; doc.addGrid(makeGrid());
        const SnapResult r = SnapEngine::snap({30.0, 1010.0, 0.0}, doc, tol, true, true,
                                              std::optional<Vec3>{Vec3{2000.0, 1000.0, 0.0}}, &allOn);
        check(r.type == SnapType::Perpendicular, "6a referansla: Perpendicular snap'i");
        check(approx(r.point.x, 0.0) && approx(r.point.y, 1000.0), "6b dikme ayagi: (0,1000)");
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
        check(approx(r.point.x, 0.0) && approx(r.point.y, 0.0), "8b tolerans disi: en yakin kesisim (0,0)");
    }

    // --- 9) OBJE SNAP'I GRID'I EZER (obje yolu once calisir) ---
    {
        Document doc;
        doc.addGrid(makeGrid());
        doc.addModel(WireframeModel::line({100.0, 1000.0, 0.0}, {500.0, 1000.0, 0.0}));
        const SnapResult r = SnapEngine::snap({280.0, 1005.0, 0.0}, doc, tol, true, true,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Midpoint, "9a obje snap'i kazanir (grid'e kaymaz)");
        check(approx(r.point.x, 300.0) && approx(r.point.y, 1000.0), "9b obje orta noktasi (300,1000)");
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

    // ================= 2 NOKTALI ELEVASYON (XZ/YZ) GRID SNAP =================
    // KULLANICI SARTI: "XZ YZ gibi 2 noktayla alinan goruntulerde grid sadece
    // xy deki alt grid noktalarina snap oluyor; her grid snap ozelligine
    // kilitlenmeli". Kok neden: snap `grid.axes` (PLAN tabani; tum plan
    // cizgileri z=origin.z'de) kullaniyordu, cizim ise `gridAxesForView`.
    // Artik snap de cizimle AYNI gorunus duzlemini kullanir.
    const int W = 1200, H = 800;
    auto xzCamera = [] {
        Camera c;
        // ekran saga +X, ekran yukari +Z (bakış Y) -> gridViewDirection (0,-1,0)
        c.setViewBasis(Vec3{1, 0, 0}, Vec3{0, 0, 1}, Vec3{6000, 0, 3600});
        return c;
    };
    auto yzCamera = [] {
        Camera c;
        // ekran saga +Y, ekran yukari +Z (bakış X) -> gridViewDirection (1,0,0)
        c.setViewBasis(Vec3{0, 1, 0}, Vec3{0, 0, 1}, Vec3{0, 2500, 3600});
        return c;
    };

    // --- 12) XZ: DIKEY aks uzerinde, kot 2000 (tabana cekilmemeli) ---
    {
        Camera cam = xzCamera();
        Document doc; doc.addGrid(makeLevelGrid());
        const Vec2 s = cam.project({0.0, 0.0, 2000.0}, W, H);
        const WorkPlane wp = WorkPlane::fromViewDirection(Vec3{0, 0, 0}, cam.gridViewDirection());
        const SnapResult r = SnapEngine::snap3D(s, doc, cam, W, H, 30.0, wp, true, true,
                                                std::nullopt, &allOn, false);
        check(r.type == SnapType::Nearest, "12a XZ: dikey aks uzerinde Nearest");
        check(approx(r.point.x, 0.0) && approx(r.point.y, 0.0) && approx(r.point.z, 2000.0),
              "12b XZ: nokta (0,0,2000) — tabana (z=0) CEKILMEZ");
    }

    // --- 13) XZ: KAT cizgisi (z=3600) uzerinde ---
    {
        Camera cam = xzCamera();
        Document doc; doc.addGrid(makeLevelGrid());
        const Vec2 s = cam.project({4000.0, 0.0, 3600.0}, W, H);
        const WorkPlane wp = WorkPlane::fromViewDirection(Vec3{0, 0, 0}, cam.gridViewDirection());
        const SnapResult r = SnapEngine::snap3D(s, doc, cam, W, H, 30.0, wp, true, true,
                                                std::nullopt, &allOn, false);
        check(r.type == SnapType::Nearest, "13a XZ: kat cizgisi uzerinde Nearest");
        check(approx(r.point.x, 4000.0) && approx(r.point.z, 3600.0),
              "13b XZ: nokta (4000,0,3600) — kat cizgisi");
    }

    // --- 14) YZ: dikey aks uzerinde, kot 5000 ---
    {
        Camera cam = yzCamera();
        Document doc; doc.addGrid(makeLevelGrid());
        const Vec2 s = cam.project({0.0, 0.0, 5000.0}, W, H);
        const WorkPlane wp = WorkPlane::fromViewDirection(Vec3{0, 0, 0}, cam.gridViewDirection());
        const SnapResult r = SnapEngine::snap3D(s, doc, cam, W, H, 30.0, wp, true, true,
                                                std::nullopt, &allOn, false);
        check(r.type == SnapType::Nearest, "14a YZ: dikey aks uzerinde Nearest");
        check(approx(r.point.y, 0.0) && approx(r.point.z, 5000.0),
              "14b YZ: nokta (0,0,5000) — tabana cekilmez");
    }

    // --- 15) XZ: KAVSAK (capraz) dogru KOTTA olusur ---
    // (12000,0,7200) hicbir orta noktayla cakismaz -> saf SnapType::Grid.
    {
        Camera cam = xzCamera();
        Document doc; doc.addGrid(makeLevelGrid());
        const Vec2 s = cam.project({12000.0, 0.0, 7200.0}, W, H);
        const WorkPlane wp = WorkPlane::fromViewDirection(Vec3{0, 0, 0}, cam.gridViewDirection());
        const SnapResult r = SnapEngine::snap3D(s, doc, cam, W, H, 30.0, wp, true, true,
                                                std::nullopt, &allOn, false);
        check(r.type == SnapType::Grid, "15a XZ: kavsakta SnapType::Grid");
        check(approx(r.point.x, 12000.0) && approx(r.point.z, 7200.0),
              "15b XZ: kavsak (12000,0,7200) — eski plan-formulu z'yi tabana cekerdi");
    }

    // --- 16) XZ MASKE KAPISI: Nearest kapali -> kavsak kalir (görunus duzleminde) ---
    {
        Camera cam = xzCamera();
        Document doc; doc.addGrid(makeLevelGrid());
        const SnapTypeMask noNearest = maskExcept({SnapType::Nearest});
        const Vec2 s = cam.project({60.0, 0.0, 5000.0}, W, H);
        const WorkPlane wp = WorkPlane::fromViewDirection(Vec3{0, 0, 0}, cam.gridViewDirection());
        const SnapResult r = SnapEngine::snap3D(s, doc, cam, W, H, 5.0, wp, true, true,
                                                std::nullopt, &noNearest, false);
        check(r.type == SnapType::Grid,
              "16a XZ tolerans disi + Nearest kapali: gorunus duzlemindeki kavsaga gider");
        check(approx(r.point.z, 3600.0),
              "16b XZ: en yakin kavsak z=3600 (eski kod z=0 tabanina cekerdi)");
    }

    // --- 17) TABAN hala erisilebilir: kot 0 dogru cozulur ---
    {
        Camera cam = xzCamera();
        Document doc; doc.addGrid(makeLevelGrid());
        const Vec2 s = cam.project({6000.0, 0.0, 0.0}, W, H);
        const WorkPlane wp = WorkPlane::fromViewDirection(Vec3{0, 0, 0}, cam.gridViewDirection());
        const SnapResult r = SnapEngine::snap3D(s, doc, cam, W, H, 30.0, wp, true, true,
                                                std::nullopt, &allOn, false);
        check(approx(r.point.z, 0.0), "17a XZ: taban kavsagi (z=0) hala cozulur");
        check(approx(r.point.x, 6000.0), "17b XZ: dogru dikey aks (x=6000)");
    }

    std::printf("\n%d kontrol, %d hata\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
