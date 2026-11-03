// LEVHA CERCEVESI — EL YONU VE KOSE NUMARALAMA (CONTRACT.md kurali).
//
// Sabitlenen kural (libs/mm_contract/CONTRACT.md, "PlateSpec cercevesi"):
//   1. genislik u, yukseklik v boyunca,
//   2. kalinlik axis boyunca SIMETRIK (levha `center` etrafinda +/- t/2;
//      extrüzyon yonu +axis) — iki el yonunde de ayni hacim,
//   3. kose numaralari materializer nokta sirasina gore:
//      0=(-u,-v) 1=(+u,-v) 2=(+u,+v) 3=(-u,+v)
//   4. el yonu SERBEST: u x v = +axis zorunlu degil; sol elli cerceve de AYNI
//      formulle yorumlanir (materializer cerceveyi "duzeltmez").
//
// Test hem sag hem sol elli levhayi kurar; kalinlik araligini ve pahli kosede
// hangi kose noktasinin kaldirildigini dogrular. Boylece iki taraf (Model Maker
// materializer + Studio) ayni kurala baglanmis olur.

#include "model_maker/component_engine.hpp"
#include "model_maker/component_materializer.hpp"
#include "model_maker/geometry.hpp"

#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

struct Pt { double x, y, z; };

static std::vector<Pt> meshPoints(const TopoDS_Shape& shape) {
    std::vector<Pt> pts;
    if (shape.IsNull()) return pts;
    BRepMesh_IncrementalMesh mesher(shape, 0.2);
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
        const TopoDS_Face& face = TopoDS::Face(faces.Current());
        TopLoc_Location loc;
        const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) continue;
        const gp_Trsf tr = loc.Transformation();
        for (int n = 1; n <= tri->NbNodes(); ++n) {
            const gp_Pnt p = tri->Node(n).Transformed(tr);
            pts.push_back({p.X(), p.Y(), p.Z()});
        }
    }
    return pts;
}

// (x,y,z) noktasi var mi? z verilmezse z yonu yok sayilir (kalinlik araliginin
// iki yuzunde de kose bulunur).
static bool hasPoint(const std::vector<Pt>& pts, double x, double y, double z,
                     bool anyZ, double tol = 1e-6) {
    for (const auto& p : pts)
        if (std::fabs(p.x - x) <= tol && std::fabs(p.y - y) <= tol &&
            (anyZ || std::fabs(p.z - z) <= tol))
            return true;
    return false;
}

static void bboxOf(const std::vector<Pt>& pts, Pt& lo, Pt& hi) {
    lo = {1e100, 1e100, 1e100};
    hi = {-1e100, -1e100, -1e100};
    for (const auto& p : pts) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
    }
}

// width 120 (u), height 240 (v), thickness 10 -> z in [-5, +5] (center 0).
static PlateSpec makePlate(bool leftHanded, bool withChamfer, int chamferCorner) {
    PlateSpec p;
    p.center = {0.0, 0.0, 0.0};
    p.u = {1.0, 0.0, 0.0};
    p.v = {0.0, 1.0, 0.0};
    // Sag elli: u x v = +Z = axis. Sol elli: axis -Z (u x v = +Z != axis).
    p.axis = leftHanded ? Vec3{0.0, 0.0, -1.0} : Vec3{0.0, 0.0, 1.0};
    p.width = 120.0;
    p.height = 240.0;
    p.thickness = 10.0;
    if (withChamfer) {
        PlateSpec::Chamfer ch;
        ch.corner = chamferCorner;
        ch.type = 1;  // duz pah
        ch.h = 20.0;  // u bacagi
        ch.v = 30.0;  // v bacagi
        p.chamfers.push_back(ch);
    }
    return p;
}

int main() {
    const double hw = 60.0, hh = 120.0, ht = 5.0;  // t/2

    // 1) Kalinlik axis boyunca simetrik: z in [-5, +5] (sag elli).
    {
        const auto pts = meshPoints(ComponentMaterializer::buildPlateSolid(makePlate(false, false, 0)));
        Pt lo, hi;
        bboxOf(pts, lo, hi);
        check(!pts.empty(), "1 sag elli: kati uretildi");
        check(std::fabs(lo.z + ht) < 1e-6 && std::fabs(hi.z - ht) < 1e-6,
              "1 sag elli: kalinlik axis boyunca simetrik (z -5..+5)");
        check(std::fabs(lo.x + hw) < 1e-6 && std::fabs(hi.x - hw) < 1e-6,
              "1 sag elli: genislik u boyunca (+/-60)");
        check(std::fabs(lo.y + hh) < 1e-6 && std::fabs(hi.y - hh) < 1e-6,
              "1 sag elli: yukseklik v boyunca (+/-120)");
        // Dort kose de mevcut (pah yok): 0,1,2,3 — her iki z yuzunde.
        check(hasPoint(pts, -hw, -hh, -ht, false) && hasPoint(pts, -hw, -hh, ht, false),
              "1 sag elli: kose 0 (-u,-v)");
        check(hasPoint(pts, hw, -hh, -ht, false) && hasPoint(pts, hw, -hh, ht, false),
              "1 sag elli: kose 1 (+u,-v)");
        check(hasPoint(pts, hw, hh, -ht, false) && hasPoint(pts, hw, hh, ht, false),
              "1 sag elli: kose 2 (+u,+v)");
        check(hasPoint(pts, -hw, hh, -ht, false) && hasPoint(pts, -hw, hh, ht, false),
              "1 sag elli: kose 3 (-u,+v)");
    }

    // 2) Sol elli (u x v = -axis): ayni hacim, ayni koseler. Cerceveyi
    //    "duzeltme" YOK (genislik/yukseklik/kalinlik ayni).
    {
        const auto pts = meshPoints(ComponentMaterializer::buildPlateSolid(makePlate(true, false, 0)));
        Pt lo, hi;
        bboxOf(pts, lo, hi);
        check(!pts.empty(), "2 sol elli: kati uretildi");
        check(std::fabs(lo.z + ht) < 1e-6 && std::fabs(hi.z - ht) < 1e-6,
              "2 sol elli: kalinlik axis boyunca simetrik (degismez)");
        check(std::fabs(lo.x + hw) < 1e-6 && std::fabs(hi.x - hw) < 1e-6,
              "2 sol elli: genislik u boyunca (degismez)");
        check(std::fabs(lo.y + hh) < 1e-6 && std::fabs(hi.y - hh) < 1e-6,
              "2 sol elli: yukseklik v boyunca (degismez)");
        check(hasPoint(pts, -hw, -hh, 0.0, true) && hasPoint(pts, hw, hh, 0.0, true),
              "2 sol elli: kose 0 ve 2 yerinde");
    }

    // 3) Pah kose numarasi materializer sirasina gore: corner 2 pahi (+u,+v)
    //    kosesini kaldirir; diger uc kose yerinde kalir. Her iki elde AYNI.
    for (const bool left : {false, true}) {
        const auto pts = meshPoints(ComponentMaterializer::buildPlateSolid(makePlate(left, true, 2)));
        const char* hand = left ? "sol elli" : "sag elli";
        char label[160];
        std::snprintf(label, sizeof(label), "3 %s: pah corner 2 -> (+u,+v) kosesi kaldirildi", hand);
        check(!hasPoint(pts, hw, hh, 0.0, true), label);
        std::snprintf(label, sizeof(label), "3 %s: corner 0/1/3 yerinde", hand);
        check(hasPoint(pts, -hw, -hh, 0.0, true) && hasPoint(pts, hw, -hh, 0.0, true) &&
                  hasPoint(pts, -hw, hh, 0.0, true),
              label);
        // Pah bacaklari: h=20 u boyunca -> (40,120), v=30 v boyunca -> (60,90).
        std::snprintf(label, sizeof(label), "3 %s: pah bacaklari h(u)/v(v) dogru yerde", hand);
        check(hasPoint(pts, hw - 20.0, hh, 0.0, true) && hasPoint(pts, hw, hh - 30.0, 0.0, true),
              label);
    }

    // 4) corner 0 pahi (-u,-v) kosesini kaldirir (numaralandirma gercekten
    //    materializer sirasina gore; aynalama yok).
    for (const bool left : {false, true}) {
        const auto pts = meshPoints(ComponentMaterializer::buildPlateSolid(makePlate(left, true, 0)));
        const char* hand = left ? "sol elli" : "sag elli";
        char label[160];
        std::snprintf(label, sizeof(label), "4 %s: pah corner 0 -> (-u,-v) kosesi kaldirildi", hand);
        check(!hasPoint(pts, -hw, -hh, 0.0, true), label);
        std::snprintf(label, sizeof(label), "4 %s: (+u,+v) kosesi yerinde", hand);
        check(hasPoint(pts, hw, hh, 0.0, true), label);
    }

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
