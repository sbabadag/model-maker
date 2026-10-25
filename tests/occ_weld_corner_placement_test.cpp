#ifdef MM_HAS_OCC

// Regresyon: levha kenari kolonun KOSESINE (iki kolon yuzunun birlesme cizgisi)
// denk geldiginde kaynak kesiti yanlis yuze gore kurulup kolonun ICINE
// dusuyordu. Burada levha, kenari tam kolon kosеsine gelecek sekilde
// yerlestirilir; kaynak katisinin hacim merkezi iki parcanin da DISINDA olmali.

#include "model_maker/weld.hpp"
#include "model_maker/weld_geometry.hpp"

#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <gp_Pnt.hxx>
#include <TopoDS_Shape.hxx>

#include <cstdio>
#include <string>

using namespace mm;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

bool inside(const gp_Pnt& p, const TopoDS_Shape& s) {
    BRepClass3d_SolidClassifier cls(s);
    cls.Perform(p, 1e-6);
    return cls.State() == TopAbs_IN;
}

gp_Pnt centroid(const TopoDS_Shape& s) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(s, props);
    return props.CentreOfMass();
}

// Kolon: 300x300x3000 kutu, x[-150,150] y[-300,0] z[0,3000].
TopoDS_Shape makeColumn() {
    return BRepPrimAPI_MakeBox(gp_Pnt(-150.0, -300.0, 0.0), 300.0, 300.0, 3000.0).Shape();
}

} // namespace

int main() {
    const TopoDS_Shape column = makeColumn();

    // Senaryo A: levha kolonun +X yuzune yasli, ust kenari TAM kolonun y=0
    // kosе cizgisine denk geliyor (kullanicinin gordugu durum).
    {
        const TopoDS_Shape plate =
            BRepPrimAPI_MakeBox(gp_Pnt(150.0, -120.0, 1200.0), 10.0, 120.0, 300.0).Shape();
        Weld w;
        w.type = WeldType::Fillet;
        w.size = 5.0;
        w.doubleSided = false;
        std::string reason;
        const auto solids = buildWeldSolids(w, column, plate, 5.0, reason);
        check(!solids.empty() && reason.empty(), "kose senaryosu: kaynak katisi uretildi");
        if (!solids.empty()) {
            const gp_Pnt c = centroid(solids[0]);
            std::printf("  kaynak merkez = (%.2f, %.2f, %.2f)\n", c.X(), c.Y(), c.Z());
            check(!inside(c, column), "kose senaryosu: kaynak kolonun ICINDE DEGIL");
            check(!inside(c, plate), "kose senaryosu: kaynak levhanin ICINDE DEGIL");
        } else {
            std::printf("  neden='%s'\n", reason.c_str());
        }
    }

    // Senaryo B: levha kolonun y=0 kosеsine ORTALANMIS (kenar kosе cizgisini
    // iki yana tasiyor) — eski kod burada da yanlis yuz secebiliyordu.
    {
        const TopoDS_Shape plate =
            BRepPrimAPI_MakeBox(gp_Pnt(150.0, -60.0, 1200.0), 10.0, 120.0, 300.0).Shape();
        Weld w;
        w.type = WeldType::Fillet;
        w.size = 5.0;
        w.doubleSided = false;
        std::string reason;
        const auto solids = buildWeldSolids(w, column, plate, 5.0, reason);
        check(!solids.empty() && reason.empty(), "orta kose senaryosu: kaynak katisi uretildi");
        if (!solids.empty()) {
            const gp_Pnt c = centroid(solids[0]);
            std::printf("  kaynak merkez = (%.2f, %.2f, %.2f)\n", c.X(), c.Y(), c.Z());
            check(!inside(c, column), "orta kose senaryosu: kaynak kolonun ICINDE DEGIL");
            check(!inside(c, plate), "orta kose senaryosu: kaynak levhanin ICINDE DEGIL");
        } else {
            std::printf("  neden='%s'\n", reason.c_str());
        }
    }

    // Senaryo C: ters secim sirasi (ana=levha, ikincil=kolon) de disarida kalmali.
    {
        const TopoDS_Shape plate =
            BRepPrimAPI_MakeBox(gp_Pnt(150.0, -120.0, 1200.0), 10.0, 120.0, 300.0).Shape();
        Weld w;
        w.type = WeldType::Fillet;
        w.size = 5.0;
        w.doubleSided = false;
        std::string reason;
        const auto solids = buildWeldSolids(w, plate, column, 5.0, reason);
        check(!solids.empty() && reason.empty(), "ters sira: kaynak katisi uretildi");
        if (!solids.empty()) {
            const gp_Pnt c = centroid(solids[0]);
            std::printf("  kaynak merkez = (%.2f, %.2f, %.2f)\n", c.X(), c.Y(), c.Z());
            check(!inside(c, column), "ters sira: kaynak kolonun ICINDE DEGIL");
            check(!inside(c, plate), "ters sira: kaynak levhanin ICINDE DEGIL");
        } else {
            std::printf("  neden='%s'\n", reason.c_str());
        }
    }

    if (failures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}

#endif // MM_HAS_OCC
