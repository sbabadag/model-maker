// PROFIL DONUSU — AGIRLIK MERKEZI SABITLIGI (gercek OCCT).
//
// KULLANICI SARTI: Props'tan aci yazilinca kesit KENDI agirlik merkezi etrafinda
// donmeli, merkez YER DEGISTIRMEMELI. Top/Bottom ankrajli kesitte merkez eksenden
// ofsetli oldugu icin eski kod (sabit eksen + donme) merkezi ortalatiyordu;
// profileRotationCentroidShift ekseni ters yonde kaydirarak merkezi sabit tutar.
//
// Bu test su zinciri kanitlar (HEA200, Top ankraj, X ekseni):
//   C1. extrude(90°, eksen orijin) -> kesit merkezi (0, +95, z) (anchor + donme).
//   C2. profileRotationCentroidShift(90->0) == (0, +95, +95)  (eksen kaymasi).
//   C3. extrude(0°, eksen + shift)  -> kesit merkezi AYNI (0, +95, z) kalir.
//   C4. Center ankraj: aci degisse bile kayma {0,0,0} (merkez = eksen).
//   C5. Aci degismediyse kayma {0,0,0}.
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-3) { return std::abs(a - b) < tol; }
static bool approx3(const Vec3& a, double x, double y, double z, double tol = 1e-3) {
    return approx(a.x, x, tol) && approx(a.y, y, tol) && approx(a.z, z, tol);
}

static gp_Pnt solidCentroid(const TopoDS_Shape& s) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(s, props);
    return props.CentreOfMass();
}

int main() {
    SteelProfile p;
    p.name = "HEA200";
    p.height = 190.0;
    p.width = 200.0;
    p.plateThickness = 6.5;
    p.flangeThickness = 10.0;

    const Vec3 from{0.0, 0.0, 0.0};
    const Vec3 to{3000.0, 0.0, 0.0};
    const Vec3 axisDir{1.0, 0.0, 0.0};
    const Vec3 refNormal{0.0, 0.0, 1.0};

    // C1: Top ankraj + 90° donmede kesit merkezi eksene gore +95 (width) kayar.
    {
        const TopoDS_Shape s =
            extrudeProfileSolid(p, from, to, 90.0, refNormal, SectionAnchor::Top);
        const gp_Pnt c = solidCentroid(s);
        // Uye +X boyunca (orta X=1500), kesit merkezi +Y (95), Z=0.
        check(approx(c.X(), 1500.0) && approx(c.Y(), 95.0) && approx(c.Z(), 0.0),
              "C1 Top+90° kesit merkezi (1500, +95, 0)");
    }

    // C2: 90 -> 0 donusunde eksen kaymasi (0, +95, +95).
    {
        const Vec3 shift =
            profileRotationCentroidShift(p, SectionAnchor::Top, axisDir, refNormal, 90.0, 0.0);
        check(approx3(shift, 0.0, 95.0, 95.0), "C2 shift(90->0) == (0, +95, +95)");
    }

    // C3: kaydirilmis eksenle 0° ekstrude -> kesit merkezi AYNI kalir.
    {
        const Vec3 shift =
            profileRotationCentroidShift(p, SectionAnchor::Top, axisDir, refNormal, 90.0, 0.0);
        const Vec3 shiftedFrom{from.x + shift.x, from.y + shift.y, from.z + shift.z};
        const Vec3 shiftedTo{to.x + shift.x, to.y + shift.y, to.z + shift.z};
        const TopoDS_Shape s =
            extrudeProfileSolid(p, shiftedFrom, shiftedTo, 0.0, refNormal, SectionAnchor::Top);
        const gp_Pnt c = solidCentroid(s);
        check(approx(c.X(), 1500.0) && approx(c.Y(), 95.0) && approx(c.Z(), 0.0),
              "C3 0°+shift merkez SABIT (1500, +95, 0)");
    }

    // C4: Center ankraj — aci degisse bile kayma yok (merkez eksende).
    {
        const Vec3 shift =
            profileRotationCentroidShift(p, SectionAnchor::Center, axisDir, refNormal, 90.0, 0.0);
        check(approx3(shift, 0.0, 0.0, 0.0), "C4 Center ankraj shift == (0,0,0)");
    }

    // C5: Aci degismediyse kayma yok.
    {
        const Vec3 shift =
            profileRotationCentroidShift(p, SectionAnchor::Top, axisDir, refNormal, 45.0, 45.0);
        check(approx3(shift, 0.0, 0.0, 0.0), "C5 ayni aci shift == (0,0,0)");
    }

    if (failures) {
        std::printf("%d kontrol basarisiz\n", failures);
        return 1;
    }
    std::printf("PROFIL DONUSU MERKEZ SABITLIGI: TUM KONTROLLER GECTI\n");
    return 0;
}
