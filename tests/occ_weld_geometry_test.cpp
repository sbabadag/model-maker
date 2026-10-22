// GERCEK OCC KAYNAK KATISI kabul testi (Phase W3).
// buildFilletWeldSolid'un hacmi/dogrulugu, findWeldJunctions'un temas kenari
// tespiti, buildWeldSolids'un tek/cift tarafli katilari ve ayrik parcalarda
// guvenli fallback — gercek OpenCASCADE ile sayisal dogrulanir.
//
// Derleme: build-occ-linux uzerinden (ctest) — bkz CMakeLists.txt.

#include "model_maker/weld_geometry.hpp"
#include "model_maker/weld.hpp"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Shape.hxx>

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static double vol(const TopoDS_Shape& s) {
    if (s.IsNull()) return -1.0;
    GProp_GProps p;
    BRepGProp::VolumeProperties(s, p);
    return p.Mass();
}

static bool validSolid(const TopoDS_Shape& s) {
    if (s.IsNull()) return false;
    BRepCheck_Analyzer an(s);
    return an.IsValid();
}

static void bbox(const TopoDS_Shape& s, double& xmin, double& ymin, double& zmin,
                 double& xmax, double& ymax, double& zmax) {
    Bnd_Box b;
    BRepBndLib::Add(s, b);
    b.Get(xmin, ymin, zmin, xmax, ymax, zmax);
}

int main() {
    // --- 1) Esit bacakli fillet kati (90° kose, dogrudan normal'lerle) --------
    // nm=(1,0,0) ana yuz, ns=(0,1,0) ikincil yuz, d=(0,0,1); kose (x>0,y>0).
    {
        const Vec3 start{0, 0, 0}, end{0, 0, 100};
        const Vec3 nm{1, 0, 0}, ns{0, 1, 0};
        TopoDS_Shape s5 = buildFilletWeldSolid(start, end, nm, ns, 5.0, true);
        check(!s5.IsNull(), "fillet 5mm solid uretildi");
        check(validSolid(s5), "fillet 5mm BRepCheck gecerli");
        const double v5 = vol(s5);
        // Ucgen kesit alani = 0.5*5*5 = 12.5; hacim = 12.5 * 100 = 1250.
        std::printf("  fillet 5mm hacim = %.3f (beklenen 1250.0)\n", v5);
        check(std::fabs(v5 - 1250.0) < 1.0, "fillet 5mm hacim = 1250 mm^3");

        TopoDS_Shape s6 = buildFilletWeldSolid(start, end, nm, ns, 6.0, true);
        const double v6 = vol(s6);
        std::printf("  fillet 6mm hacim = %.3f (beklenen 1800.0)\n", v6);
        check(std::fabs(v6 - 1800.0) < 1.0, "fillet 6mm hacim = 1800 mm^3 (boyut degisimi)");
        check(v6 > v5, "boyut artinca hacim artar");
    }

    // --- 2) T-joint temas kenari tespiti (kolon + levha) ----------------------
    // Kolon: x in [-100,0], y in [-100,100], z in [0,100].
    // Levha: x in [0,50], y in [-5,5], z in [40,60] (yuzu kolon yuzune dayali).
    TopoDS_Shape col = BRepPrimAPI_MakeBox(gp_Pnt(-100, -100, 0), 100, 200, 100).Solid();
    TopoDS_Shape plate = BRepPrimAPI_MakeBox(gp_Pnt(0, -5, 40), 50, 10, 20).Solid();
    {
        auto juncts = findWeldJunctions(plate, col, 0.5);
        std::printf("  T-joint temas kenar sayisi = %zu\n", juncts.size());
        check(juncts.size() >= 4, "T-joint: 4 temas kenari (levha yuzunun sinirlari)");
        if (!juncts.empty()) {
            std::printf("  en uzun kenar = %.3f (beklenen 20.0)\n", juncts[0].length);
            check(std::fabs(juncts[0].length - 20.0) < 0.01, "en uzun temas kenari 20 mm");
        }
    }

    // --- 3) buildWeldSolids: TAM CEVRE (TAM KAYNAK) ---------------------------
    // Levha yuzunun 4 sinir kenari da kolona temas eder (2 x 20mm + 2 x 10mm)
    // -> her kenara bir fillet. Eski davranis en uzun TEK kenara uretip geri
    // kalanlari atiyordu ("bir kenarda kaynak var, digerlerinde yok" hatasi);
    // kullanici tam cevres kaynak bekliyor (146 diyalogunun Welds tablosu).
    {
        Weld w;
        w.type = WeldType::Fillet;
        w.doubleSided = false;
        std::string reason;
        auto single = buildWeldSolids(w, col, plate, 5.0, reason, nullptr);
        std::printf("  single: %zu kati, neden='%s'\n", single.size(), reason.c_str());
        check(single.size() == 4, "tam cevres: 4 kati (4 temas kenari)");
        bool allValid = true;
        double totV = 0.0;
        for (const auto& sol : single) {
            if (!validSolid(sol)) allValid = false;
            totV += vol(sol);
        }
        check(allValid, "tam cevres: hepsi BRepCheck gecerli");
        std::printf("  tam cevres toplam hacim = %.3f (beklenen 750.0)\n", totV);
        check(std::fabs(totV - 750.0) < 1.0, "tam cevres toplam hacim = 750 mm^3 (250+250+125+125)");
        // Her katı levhanin DISINDA: kolon x<=0 oldugu icin kaynak katilari x >= 0
        // olmalari gerekir; bacak 5mm oldugu icin x en cok 5'e uzanabilir.
        bool xOutside = true;
        for (const auto& sol : single) {
            double cx0, cy0, cz0, cx1, cy1, cz1;
            bbox(sol, cx0, cy0, cz0, cx1, cy1, cz1);
            if (cx0 < -0.05 || cx1 > 5.05) { xOutside = false; break; }
        }
        check(xOutside, "tum kaynak katilari kolonun disinda (x:[0,5], bacak 5mm)");

        // doubleSided MMW8'de korunur ama geometri değişmiyor: tam çe vre
        // zaten her kenari kapsar (her kesitte tek fillet — geometri karar verir).
        Weld wd;
        wd.type = WeldType::Fillet;
        wd.doubleSided = true;
        std::string reason2;
        auto dbl = buildWeldSolids(wd, col, plate, 5.0, reason2, nullptr);
        std::printf("  double: %zu kati, neden='%s'\n", dbl.size(), reason2.c_str());
        double dblV = 0.0;
        for (const auto& sol : dbl) dblV += vol(sol);
        check(dbl.size() == 4, "doubleSided: hala 4 kati (tam cevre)");
        check(std::fabs(dblV - 750.0) < 1.0, "doubleSided toplam hacim 750 mm^3");
    }

    // --- 4) Ayrik parcalar -> guvenli fallback (kati uretilmez) --------------
    {
        TopoDS_Shape far = BRepPrimAPI_MakeBox(gp_Pnt(1000, 1000, 1000), 50, 50, 50).Solid();
        auto juncts = findWeldJunctions(far, col, 0.5);
        check(juncts.empty(), "ayrik parcalar: temas kenari yok");
        Weld w;
        w.type = WeldType::Fillet;
        std::string reason;
        auto solids = buildWeldSolids(w, col, far, 5.0, reason);
        check(solids.empty(), "ayrik parcalar: kati uretilmez");
        check(!reason.empty(), "ayrik parcalar: neden raporlanir");
        std::printf("  ayrik neden='%s'\n", reason.c_str());
    }

    // --- 5) Butt kaynak -> unsupported (kati uretilmez) ----------------------
    {
        Weld w;
        w.type = WeldType::Butt;
        std::string reason;
        auto solids = buildWeldSolids(w, col, plate, 5.0, reason);
        check(solids.empty(), "butt kaynak: kati uretilmez");
        check(!reason.empty(), "butt kaynak: neden raporlanir");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
