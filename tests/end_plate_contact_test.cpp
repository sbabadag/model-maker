// END PLATE TEMAS YUZEYI (Phase R1) — cekirdek test.
//
// P0 duzeltmesini kilitler: levha, kolonun kirise bakan SINIR DUZLEMINE (bbox
// yuzu) gap ile oturur; kolonun icine girmez. resolveBoundaryPlaneFacing +
// computeEndPlatePlacementOnFace davranisi dogrulanir. OCC'siz, Linux'ta kosar.

#include "model_maker/end_plate.hpp"
#include "model_maker/reference_geometry.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int g_failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++g_failures;
}
static bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) < tol; }

int main() {
    // --- 1) resolveBoundaryPlaneFacing: dogru bbox yuzunu secer ---------------
    {
        const Vec3 bmin{-150.0, -150.0, 0.0};
        const Vec3 bmax{150.0, 150.0, 3000.0};

        const ReferencePlane px = resolveBoundaryPlaneFacing(bmin, bmax, {1.0, 0.0, 0.0});
        check(near(px.normal.x, 1.0) && near(px.normal.y, 0.0) && near(px.normal.z, 0.0),
              "+X yonune bakan yuz: normal +X");
        check(near(px.origin.x, 150.0) && near(px.origin.y, 0.0) && near(px.origin.z, 1500.0),
              "+X yuzu origin = x=150 (yuz merkezi)");

        const ReferencePlane ny = resolveBoundaryPlaneFacing(bmin, bmax, {0.0, -1.0, 0.0});
        check(near(ny.normal.y, -1.0), "-Y yonune bakan yuz: normal -Y");
        check(near(ny.origin.y, -150.0), "-Y yuzu origin = y=-150");

        const ReferencePlane nz = resolveBoundaryPlaneFacing(bmin, bmax, {0.0, 0.0, -1.0});
        check(near(nz.normal.z, -1.0), "-Z yonune bakan yuz: normal -Z");
        check(near(nz.origin.z, 0.0), "-Z yuzu origin = z=0");

        // Sifir yon: +X varsayilir (dejenere degil).
        const ReferencePlane zero = resolveBoundaryPlaneFacing(bmin, bmax, {0.0, 0.0, 0.0});
        check(near(zero.normal.x, 1.0), "sifir yon -> +X varsayilan yuz");
    }

    // --- 2) computeEndPlatePlacementOnFace: gap=0 -> yuzeyle CAKISMA ----------
    {
        const ReferencePlane face{{150.0, 0.0, 1500.0}, {1.0, 0.0, 0.0}};  // +X yuzu
        EndPlateParams p;
        p.plateThickness = 12.0;
        p.plateExtra = 20.0;
        p.boltCountX = 2;
        p.boltCountY = 2;
        p.gap = 0.0;

        const EndPlatePlacement pl =
            computeEndPlatePlacementOnFace(face, p.gap, 200.0, 300.0, p);

        // Kalinlik yonu = yuzey normali (+X).
        check(near(pl.axis.x, 1.0) && near(pl.axis.y, 0.0) && near(pl.axis.z, 0.0),
              "levha kalinlik yonu = +X (kirise dogru)");
        // Merkez = yuzey + (gap + T/2); kolon tarafi yuz = merkez - T/2 = yuzey + gap.
        check(near(pl.plateCenter.x, 150.0 + 6.0), "merkez x = 156 (gap=0)");
        const double colSide = pl.plateCenter.x - pl.axis.x * p.plateThickness * 0.5;
        check(near(colSide, 150.0), "gap=0: levha kolon yuzu x=150 ile CAKISIR");

        // Levha kolonun ICINE girmez: kolon tarafi yuz >= kolon yuzeyi (gap>=0).
        check(colSide >= 150.0 - 1e-9, "levha kolonun icine girmiyor");
    }

    // --- 3) gap=10 -> dogru yonde 10 mm bosluk --------------------------------
    {
        const ReferencePlane face{{150.0, 0.0, 1500.0}, {1.0, 0.0, 0.0}};
        EndPlateParams p;
        p.plateThickness = 12.0;
        p.plateExtra = 20.0;
        p.gap = 10.0;

        const EndPlatePlacement pl =
            computeEndPlatePlacementOnFace(face, p.gap, 200.0, 300.0, p);
        const double colSide = pl.plateCenter.x - pl.axis.x * p.plateThickness * 0.5;
        check(near(colSide, 160.0), "gap=10: levha kolon yuzu x=160 (10mm bosluk)");
        check(near(pl.plateCenter.x, 150.0 + 10.0 + 6.0), "merkez x = 166");
    }

    // --- 4) -X yuzune kiran kiris: normal -X, levha -X'e dogru acilir ---------
    {
        const ReferencePlane face{{-150.0, 0.0, 1500.0}, {-1.0, 0.0, 0.0}};
        EndPlateParams p;
        p.plateThickness = 12.0;
        p.plateExtra = 20.0;
        p.gap = 0.0;

        const EndPlatePlacement pl =
            computeEndPlatePlacementOnFace(face, p.gap, 200.0, 300.0, p);
        check(near(pl.axis.x, -1.0), "-X yuzunde kalinlik yonu -X");
        const double colSide = pl.plateCenter.x - pl.axis.x * p.plateThickness * 0.5;
        check(near(colSide, -150.0), "-X yuzunde levha x=-150 ile CAKISIR");
    }

    // --- 5) Bulon ekseni yuzeye DIK (levha + kolon yuzlerine dik) -------------
    {
        const ReferencePlane face{{150.0, 0.0, 1500.0}, {1.0, 0.0, 0.0}};
        EndPlateParams p;
        p.plateThickness = 12.0;
        p.plateExtra = 20.0;
        p.boltCountX = 2;
        p.boltCountY = 4;
        p.gap = 0.0;

        const EndPlatePlacement pl =
            computeEndPlatePlacementOnFace(face, p.gap, 200.0, 300.0, p);
        // Bulon ekseni = levha axis = yuzey normali.
        check(near(pl.bolts.axis.x, 1.0) && near(pl.bolts.axis.y, 0.0) &&
                  near(pl.bolts.axis.z, 0.0),
              "bulon ekseni yuzeye dik (normal +X)");
        check(pl.bolts.countX == 2 && pl.bolts.countY == 4, "2x4 bulon dizilimi");
    }

    std::printf("FAILURES: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
