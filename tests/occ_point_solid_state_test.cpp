#ifdef MM_HAS_OCC

// pointSolidState (BRepClass3d'ye girmeyen nokta siniflandirmasi) dogrulamasi.
//
// Neden ayri yol: MSYS2 opencascade 7.9.3-3 (GCC 16 -O3) BRepClass3d_SClassifier'i
// yanlis derliyor -> Windows'ta Extrema_ExtCC::Perform SIGSEGV (146 kaynak
// cokmesi). Kaynak kodu artik BRepClass3d kullanmiyor. Linux'taki OCCT saglam
// derlendigi icin burada BRepClass3d_SolidClassifier ORACLE olarak kullanilir:
// yeni yontem, kutu / silindir / delikli kutu / L-kesit uzerinde bir izgara
// noktasinin hepsinde oracle ile ayni karari vermeli.

#include "model_maker/occ_geometry.hpp"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>

#include <cstdio>
#include <string>

using namespace mm;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

int oracle(const TopoDS_Shape& s, const Vec3& p, double tol) {
    BRepClass3d_SolidClassifier cls(s);
    cls.Perform(gp_Pnt(p.x, p.y, p.z), tol);
    switch (cls.State()) {
        case TopAbs_IN: return 1;
        case TopAbs_ON: return 0;
        default: return -1;
    }
}

// Izgara: sinira tol'dan yakin noktalar ON/IN/OUT arasinda belirsiz olabilir;
// izgara adimi sinirlara denk gelmeyecek sekilde kaydirilmistir (0.37 ofset).
void compareGrid(const std::string& name, const TopoDS_Shape& s, double x0, double x1,
                 double y0, double y1, double z0, double z1, int n) {
    int total = 0, mismatch = 0, inside = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < n; ++k) {
                const Vec3 p{x0 + (x1 - x0) * (i + 0.37) / n, y0 + (y1 - y0) * (j + 0.37) / n,
                             z0 + (z1 - z0) * (k + 0.37) / n};
                const int a = pointSolidState(s, p, 1e-6);
                const int b = oracle(s, p, 1e-6);
                ++total;
                if (b == 1) ++inside;
                if (a != b) {
                    if (mismatch < 5)
                        std::printf("  mismatch %s p=(%.3f,%.3f,%.3f) safe=%d oracle=%d\n",
                                    name.c_str(), p.x, p.y, p.z, a, b);
                    ++mismatch;
                }
            }
    check(mismatch == 0 && inside > 0 && inside < total,
          name + ": " + std::to_string(total) + " nokta, " + std::to_string(inside) +
              " icerde, uyusmazlik " + std::to_string(mismatch));
}

}  // namespace

int main() {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(100.0, 60.0, 20.0).Shape();
    compareGrid("kutu", box, -10, 110, -10, 70, -5, 25, 12);

    const TopoDS_Shape cyl =
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 15.0, 40.0).Shape();
    compareGrid("silindir", cyl, -20, 20, -20, 20, -5, 45, 12);

    // Delikli levha (bulon deligi): delik ICI disarida sayilmali.
    const TopoDS_Shape hole =
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(50, 30, -1), gp_Dir(0, 0, 1)), 11.0, 22.0).Shape();
    const TopoDS_Shape plate = BRepAlgoAPI_Cut(box, hole).Shape();
    compareGrid("delikli levha", plate, 30, 70, 10, 50, -2, 22, 12);
    check(pointSolidState(plate, Vec3{50, 30, 10}) == -1, "delik merkezi DISARIDA");
    check(pointSolidState(plate, Vec3{10, 10, 10}) == 1, "levha govdesi ICERIDE");
    check(pointSolidState(plate, Vec3{10, 10, 20}) == 0, "ust yuz uzerinde YUZEYDE");

    // L kesit (iki kutu birlesimi; ic kose).
    const TopoDS_Shape leg1 = BRepPrimAPI_MakeBox(80.0, 10.0, 50.0).Shape();
    const TopoDS_Shape leg2 = BRepPrimAPI_MakeBox(10.0, 80.0, 50.0).Shape();
    const TopoDS_Shape ell = BRepAlgoAPI_Fuse(leg1, leg2).Shape();
    compareGrid("L kesit", ell, -5, 85, -5, 85, -5, 55, 12);

    // boundaryFacesOf: kati icermemeli, yuz sayisi korunmali.
    const TopoDS_Shape faces = boundaryFacesOf(plate);
    int nSolid = 0, nFaceA = 0, nFaceB = 0;
    for (TopExp_Explorer ex(faces, TopAbs_SOLID); ex.More(); ex.Next()) ++nSolid;
    for (TopExp_Explorer ex(faces, TopAbs_FACE); ex.More(); ex.Next()) ++nFaceA;
    for (TopExp_Explorer ex(plate, TopAbs_FACE); ex.More(); ex.Next()) ++nFaceB;
    check(nSolid == 0 && nFaceA == nFaceB && nFaceA > 0,
          "boundaryFacesOf: 0 kati, " + std::to_string(nFaceA) + " yuz");

    std::printf("%s (%d hata)\n", failures == 0 ? "OK" : "BASARISIZ", failures);
    return failures == 0 ? 0 : 1;
}

#else
int main() { return 0; }
#endif
