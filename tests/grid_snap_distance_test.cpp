// GRID KESİSİM SNAP'İ TOLERANS — Selahattin 2026-10-31:
// "grid kesişim noktalarına snap çok uzaktan alıyor ... diğer snapleri override
// ediyor". Eski kodda `SnapEngine::snap/snap3D` sonunda TOLERANSSIZ bir yedek
// blok vardı: görünür bir grid varsa imleç nerede olursa olsun EN YAKIN aks
// kesişimine yapışıyordu (metrelerce uzaktan). Bu dosya gerçek SnapEngine'i
// çağırır ve şunu kilitler:
//   1) kesişim yalnız objectTolerance içindeyse snap olur,
//   2) tolerans dışında snap YOK (imleç ham noktada kalır),
//   3) obje snap'i (uç nokta) grid'den önce gelir.
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
static bool approx(double a, double b, double eps = 1e-3) { return std::fabs(a - b) <= eps; }

// X: {0,6000,12000} dikey akslar, Y: {0,5000} yatay akslar.
static GridDefinition makeGrid() {
    GridDefinition g;
    g.name = "TEST";
    g.xCoords = {0.0, 6000.0, 12000.0};
    g.yCoords = {0.0, 5000.0};
    rebuildGridAxes(g);
    return g;
}

int main() {
    constexpr double kTol = 100.0;   // mm (2B)

    Document doc;
    doc.addGrid(makeGrid());
    SnapEngine engine;

    // (1) Kesisme yakin: (0,0) 50 mm -> GRID
    {
        const SnapResult r = engine.snap({30.0, -40.0, 0.0}, doc, kTol, false, true);
        std::printf("  yakin: tip=%d nokta=(%.1f %.1f)\n", (int)r.type, r.point.x, r.point.y);
        check(r.type == SnapType::Grid, "tolerans icindeki kesisim GRID'e snap olur");
        check(approx(r.point.x, 0.0) && approx(r.point.y, 0.0), "nokta kesisimin kendisi");
    }

    // (2) Kesisme tolerans DISI (3,5 m uzakta) -> snap OLMAMALI
    {
        const Vec3 cursor{3000.0, 2500.0, 0.0};
        const SnapResult r = engine.snap(cursor, doc, kTol, false, true);
        std::printf("  uzak: tip=%d nokta=(%.1f %.1f) dist=%.1f\n", (int)r.type, r.point.x,
                    r.point.y, r.distance);
        check(r.type == SnapType::None, "tolerans disindaki kesisime snap YOK");
        check(approx(r.point.x, cursor.x) && approx(r.point.y, cursor.y),
              "uzakta imlec ham noktada kalir");
    }

    // (3) Tam tolerans siniri: 80 mm -> GRID, 150 mm -> None
    {
        const SnapResult in = engine.snap({80.0, 0.0, 0.0}, doc, kTol, false, true);
        check(in.type == SnapType::Grid, "80 mm (tolerans icinde) GRID");
        // (150,150): hem kavsaktan (212 mm) hem iki aks cizgisinden (150 mm)
        // tolerans disinda -> hicbir snap olmamali.
        const SnapResult out = engine.snap({150.0, 150.0, 0.0}, doc, kTol, false, true);
        check(out.type == SnapType::None, "tolerans disi (150 mm) kavsak/cizgi snap YOK");
    }

    // (4) Obje snap'i grid'i ezer: uc nokta (3050,2500), grid kesisimleri km'lerce uzakta
    {
        doc.addModel(WireframeModel::line({3050.0, 2500.0, 0.0}, {4000.0, 2500.0, 0.0}));
        const SnapResult r = engine.snap({3060.0, 2510.0, 0.0}, doc, kTol, true, true);
        std::printf("  obje: tip=%d nokta=(%.1f %.1f)\n", (int)r.type, r.point.x, r.point.y);
        check(r.type == SnapType::Endpoint, "obje ucu grid kesisimini EZER");
        check(approx(r.point.x, 3050.0) && approx(r.point.y, 2500.0), "uc noktaya oturur");
    }

    // (5b) GRID AKS UCU vs YAKIN KOPYALANMIS OBJE UCU (Selahattin 2026-10-31:
    // "endpoint grid uclarinda tutmuyor, ozellikle kopyalanan objelerin oldugu
    // gridde"). Imlec grid ucunun TAM uzerinde (7 mm), kopyalanmis obje ucu
    // 49 mm otede -> grid ucu kazanmali. Eski kod obje snap'i bulunca hemen
    // dondugu icin 49 mm otedeki obje ucuna kaciyordu.
    {
        Document d2;
        d2.addGrid(makeGrid());            // x=0 aksi (0,-1000)->(0,6000)
        d2.addModel(WireframeModel::line({40.0, -960.0, 0.0}, {300.0, -960.0, 0.0}));
        const SnapResult atGrid = engine.snap({5.0, -995.0, 0.0}, d2, kTol, true, true);
        std::printf("  grid ucu: tip=%d nokta=(%.1f %.1f)\n", (int)atGrid.type,
                    atGrid.point.x, atGrid.point.y);
        check(approx(atGrid.point.x, 0.0) && approx(atGrid.point.y, -1000.0),
              "grid aks ucu, yakin kopya obje ucunu EZER (en yakin kazanir)");
        // Obje ucu daha yakinsa obje kazanir.
        const SnapResult atObj = engine.snap({32.0, -972.0, 0.0}, d2, kTol, true, true);
        std::printf("  obje ucu: tip=%d nokta=(%.1f %.1f)\n", (int)atObj.type,
                    atObj.point.x, atObj.point.y);
        check(approx(atObj.point.x, 40.0) && approx(atObj.point.y, -960.0),
              "obje ucu daha yakinsa obje ucu kazanir");
    }

    // (6) 3B: ayni kural — tolerans disinda kesisme snap'i YOK
    {
        Document doc3;
        GridDefinition g = makeGrid();
        g.zCoords = {0.0, 3600.0};
        rebuildGridAxes(g);
        doc3.addGrid(g);
        Camera camera;
        // Ustten bakis (top view), pivot imlecin dusacagi noktada.
        camera.setViewBasis({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {3000.0, 2500.0, 0.0});
        const WorkPlane plane = WorkPlane::world();
        // Ekran merkezine yaklasik (3000,2500) dusen bir imlec: kamera top view,
        // ekran olcegi 1 px ~ ? — tolerans 8 px, kesisim ~3000 mm uzakta.
        const SnapResult r = engine.snap3D({640.0, 360.0}, doc3, camera, 1280, 720, 8.0, plane,
                                           false, true);
        std::printf("  3B: tip=%d nokta=(%.1f %.1f)\n", (int)r.type, r.point.x, r.point.y);
        check(r.type == SnapType::None, "3B'de tolerans disindaki kesisime snap YOK");
    }

    std::printf("%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
