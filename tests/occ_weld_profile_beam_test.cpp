// GERCEK I-PROFIL T-BIRLESTIMINDE KAYNAK KATISI (W3-FIX regresyon).
//
// Kok neden (W3-FIX): cornerFaceNormal ikincil parcanin yuz normalini secerken
// kaynak yonune (d) PARALEL normali olan "uc" yuzunu (kirisin temas yuzu)
// secebiliyordu; o zaman cross(d, ns) sifir olur ve fillet kati uretilemezdi
// ("BRepCheck gecersiz"). Bu test GERCEK HEB300 kolon + IPE200 kiris
// T-birlestirmesinde (kutu degil) kati uretildigini sayisal dogrular.
//
// Derleme: build-occ-linux uzerinden (ctest) — bkz CMakeLists.txt.

#include "model_maker/weld_geometry.hpp"
#include "model_maker/weld.hpp"
#include "model_maker/profile_database.hpp"
#include "model_maker/occ_geometry.hpp"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
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

static void bbox(const TopoDS_Shape& s, double* v) {
    Bnd_Box b;
    BRepBndLib::Add(s, b);
    b.Get(v[0], v[1], v[2], v[3], v[4], v[5]);
}

static SteelProfile makeProfile(const char* name, double h, double b, double tw, double tf) {
    SteelProfile p;
    p.name = name;
    p.height = h;
    p.width = b;
    p.plateThickness = tw;
    p.flangeThickness = tf;
    return p;
}

int main() {
    const SteelProfile heb300 = makeProfile("HEB300", 300.0, 300.0, 11.0, 19.0);
    const SteelProfile ipe200 = makeProfile("IPE200", 200.0, 100.0, 5.6, 8.5);

    // Kolon: axis Z, bolum normali {0,1,0} (end-plate testiyle ayni kural).
    const TopoDS_Shape col = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0, {0, 1, 0});
    double cb[6];
    bbox(col, cb);

    // Kiris: uc yuzu kolonun +X flans yuzune (x = xmax) dayali, axis -X,
    // bolum normali Z (yukari). Web kenari (d = -Z) temas kenari olur.
    const double faceX = cb[3];
    const TopoDS_Shape beam =
        extrudeProfileSolid(ipe200, {faceX + 2000.0, 0.0, 1500.0}, {faceX, 0.0, 1500.0}, 0.0, {0, 0, 1});

    auto juncts = findWeldJunctions(beam, col, 0.5);
    check(!juncts.empty(), "I-profil T-birlestimi: dogrusal temas kenari bulundu");
    if (juncts.empty()) return failures + 1;
    std::printf("  temas kenari uzunlugu = %.2f mm\n", juncts[0].length);

    Weld w;
    w.type = WeldType::Fillet;
    w.size = 6.0;
    std::string reason;
    auto solids = buildWeldSolids(w, col, beam, 6.0, reason);
    check(solids.size() == 1, "I-profil T-birlestimi: 1 kaynak kati uretildi");
    if (solids.size() != 1) {
        std::printf("  neden='%s'\n", reason.c_str());
        return failures + 1;
    }
    BRepCheck_Analyzer an(solids[0]);
    check(an.IsValid(), "I-profil kaynak kati BRepCheck gecerli");
    const double v = vol(solids[0]);
    const double expected = 0.5 * 6.0 * 6.0 * juncts[0].length;
    std::printf("  kaynak hacmi = %.2f (beklenen ~%.2f)\n", v, expected);
    check(std::fabs(v - expected) < 1.0, "kaynak hacmi = 0.5*leg^2*uzunluk");

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
