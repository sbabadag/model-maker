// END PLATE YERLESIMI — SECTIONANCHOR DUZELTMESI KABUL TESTI (gercek OCCT).
//
// Hata: levha, kiris EKSENINE (anchor cizgisi) merkezleniyordu; Top anchor'da
// eksen kesitin USTUNDEN gecer, kesit ALTINA sarkar. Bu yuzden levha kesitin
// ustune kayiyordu (IPE200 + Top -> levha merkezi kesit merkezinden +100 mm
// yukarida, kesit levhanin altindan 80 mm tasiyordu).
//
// Duzeltme: computeEndPlatePlacementOnFace artik kesit MERKEZINE (anchor ofseti
// + kesit yukseklik yonu) oturur. Bu test levha-kiris GEOMETRIK hizasini sayisal
// dogrular: (1) levha kolonun dogru on yuzune temas eder, (2) levha merkezi
// kiris kesit merkeziyle CAKISIR (normal yonunde gap+t/2 disinda), (3) kiris
// kesit bbox'i levha boyutlari ICINDE kalir, (4) Top/Center/Bottom dogru,
// (5) 30° donmus kolon dogru, (6) farkli Z kotlari dogru, (7) bulonlar yeni
// levha icinde.
//
// Derleme (Linux + OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_end_plate_placement_acceptance.cpp \
//       src/end_plate.cpp src/end_plate_geometry.cpp src/bolt.cpp \
//       src/bolt_geometry.cpp src/geometry.cpp src/profile_database.cpp \
//       src/reference_geometry.cpp src/contact_resolver.cpp \
//       src/occ_contact_resolver.cpp src/occ_geometry.cpp src/cut_modifier.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh \
//       -lTKXSBase -lTKDESTEP \
//       -o occ_end_plate_placement_acceptance && ./occ_end_plate_placement_acceptance

#include "model_maker/end_plate.hpp"
#include "model_maker/end_plate_geometry.hpp"
#include "model_maker/occ_contact_resolver.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/bolt.hpp"
#include "model_maker/bolt_geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-3) { return std::abs(a - b) < tol; }

static SteelProfile prof(const char* name, double h, double b, double tw, double tf) {
    SteelProfile p;
    p.name = name;
    p.height = h;
    p.width = b;
    p.plateThickness = tw;
    p.flangeThickness = tf;
    return p;
}

static void solidBBox(const TopoDS_Shape& s, double& x0, double& y0, double& z0,
                      double& x1, double& y1, double& z1) {
    Bnd_Box box;
    BRepBndLib::Add(s, box);
    box.Get(x0, y0, z0, x1, y1, z1);
}

// Uretim hatti (resolveEndPlatePlacement ile AYNI — OCC temas + anchor'li
// computeEndPlatePlacementOnFace): kolon B-Rep + cerceve + kiris -> yerlesim.
struct Solve {
    ContactPlacement contact;
    EndPlatePlacement placement;
    TopoDS_Shape plate;
    bool ok{false};
};
static Solve solve(const TopoDS_Shape& colSolid, const MemberLocalFrame& colFrame,
                   const SectionDims& dims, double colLen, const Vec3& beamEnd,
                   const Vec3& beamDir, const Vec3& beamRefNormal,
                   const SteelProfile& beamProf, SectionAnchor beamAnchor,
                   const EndPlateParams& params) {
    Solve r;
    r.contact = resolveOcctContact(colSolid, colFrame, dims, colLen, beamEnd, beamDir,
                                   SemanticFaceRole::FlangeOuterPositive);
    if (r.contact.status != ResolutionStatus::Resolved) return r;
    ReferencePlane face;
    face.origin = r.contact.contactPoint;
    face.normal = r.contact.outwardNormal;
    const ProfileSectionFrame beamSec = profileSectionFrame(beamDir, beamRefNormal);
    const double anchorOff = profileSectionAnchorOffset(beamProf, beamAnchor);
    r.placement = computeEndPlatePlacementOnFace(face, params.gap, beamProf.width,
                                                 beamProf.height, params,
                                                 beamSec.height, anchorOff);
    r.plate = buildEndPlateFromPlacement(r.placement);
    r.ok = !r.plate.IsNull();
    return r;
}

int main() {
    const double cos30 = 0.86602540378443864676;
    const double sin30 = 0.5;

    const SteelProfile heb300 = prof("HEB300", 300.0, 300.0, 11.0, 19.0);
    const SteelProfile ipe200 = prof("IPE200", 200.0, 100.0, 5.6, 8.5);
    const SectionDims dims300{300.0, 300.0, 11.0, 19.0};

    EndPlateParams params;  // M20/22, 12mm levha, 2x4 bulon, gap=0
    params.gap = 0.0;
    params.boltCountX = 2;
    params.boltCountY = 4;

    // Dik kolon: axis +Z, ref normal +Y. Top anchor -> ust flans dis yuzu Y=0.
    const auto colFrame = [](const Vec3& from) {
        return makeMemberLocalFrame(profileSectionFrame({0, 0, 1}, {0, 1, 0}), from);
    };
    const TopoDS_Shape col300 = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0,
                                                    {0, 1, 0});

    // --- A: standart (Top anchor). Kiris -Y'den gelir, eksen z=1500. ---------
    // IPE200 Top anchor: kesit z=[1300,1500], merkez z=1400. Levha merkezi
    // kesit merkezine oturmali (z=1400), kolon tarafi Y=0 (gap=0).
    {
        const TopoDS_Shape beam =
            extrudeProfileSolid(ipe200, {0, 1000, 1500}, {0, 60, 1500}, 0.0, {0, 0, 1},
                                SectionAnchor::Top);
        const Solve r = solve(col300, colFrame({0, 0, 0}), dims300, 3000, {0, 500, 1500},
                              {0, -1, 0}, {0, 0, 1}, ipe200, SectionAnchor::Top, params);
        check(r.ok, "A: temas + levha uretildi");
        check(approx(r.contact.outwardNormal.y, 1.0), "A: dis normal +Y");
        check(approx(r.contact.contactPoint.y, 0.0) && approx(r.contact.contactPoint.z, 1500.0),
              "A: temas noktasi kolon ust flansi uzerinde, kiris ekseni z=1500");
        // Duzeltme kaniti: levha merkezi kiris KESIT MERKEZINE (z=1400) oturur,
        // eski (hatali) davranis z=1500 idi.
        check(approx(r.placement.plateCenter.z, 1400.0), "A: levha merkezi kesit merkezinde z=1400");
        check(approx(r.placement.plateCenter.y, 6.0), "A: levha kolon yuzunden +6 (gap=0, t=12)");

        // Levha boyutlari kiris kesitini SARAR: 140 genislik (100+2*20),
        // 240 yukseklik (200+2*20).
        check(approx(r.placement.plateWidth, 140.0) && approx(r.placement.plateHeight, 240.0),
              "A: levha boyutlari 140x240");

        // Sayisal hizalama: kiris kesit bbox'i (gercek B-Rep) levha icinde.
        double bx0, by0, bz0, bx1, by1, bz1;
        double px0, py0, pz0, px1, py1, pz1;
        solidBBox(beam, bx0, by0, bz0, bx1, by1, bz1);
        solidBBox(r.plate, px0, py0, pz0, px1, py1, pz1);
        check(approx(bz0, 1300.0) && approx(bz1, 1500.0), "A: kiris kesiti z=[1300,1500] (Top)");
        check(approx(pz0, 1280.0) && approx(pz1, 1520.0), "A: levha z=[1280,1520]");
        check(bz0 >= pz0 - 1e-3 && bz1 <= pz1 + 1e-3, "A: kiris kesiti levha YUKSEKLIGI icinde");
        check(bx0 >= px0 - 1e-3 && bx1 <= px1 + 1e-3, "A: kiris kesiti levha GENISLIGI icinde");

        // Bulonlar yeni levha icinde (origin/pattern levha merkezinden turetildi).
        const auto origins = boltGroupBoltOrigins(r.placement.bolts);
        check(origins.size() == 8, "A: 2x4 = 8 bulon");
        for (const auto& o : origins) {
            if (o.z < pz0 - 1e-3 || o.z > pz1 + 1e-3) { check(false, "A: bulon Z levha icinde"); break; }
        }
    }

    // --- B: Center anchor. Kesit z=[1400,1600], merkez z=1500. ----------------
    {
        const TopoDS_Shape beam =
            extrudeProfileSolid(ipe200, {0, 1000, 1500}, {0, 60, 1500}, 0.0, {0, 0, 1},
                                SectionAnchor::Center);
        const Solve r = solve(col300, colFrame({0, 0, 0}), dims300, 3000, {0, 500, 1500},
                              {0, -1, 0}, {0, 0, 1}, ipe200, SectionAnchor::Center, params);
        check(r.ok, "B: temas + levha uretildi");
        check(approx(r.placement.plateCenter.z, 1500.0), "B: Center anchor -> levha merkezi z=1500");
        double bx0, by0, bz0, bx1, by1, bz1, px0, py0, pz0, px1, py1, pz1;
        solidBBox(beam, bx0, by0, bz0, bx1, by1, bz1);
        solidBBox(r.plate, px0, py0, pz0, px1, py1, pz1);
        check(approx(bz0, 1400.0) && approx(bz1, 1600.0), "B: kiris kesiti z=[1400,1600] (Center)");
        check(bz0 >= pz0 - 1e-3 && bz1 <= pz1 + 1e-3, "B: kiris kesiti levha icinde");
    }

    // --- C: Bottom anchor. Kesit z=[1500,1700], merkez z=1600. ---------------
    {
        const TopoDS_Shape beam =
            extrudeProfileSolid(ipe200, {0, 1000, 1500}, {0, 60, 1500}, 0.0, {0, 0, 1},
                                SectionAnchor::Bottom);
        const Solve r = solve(col300, colFrame({0, 0, 0}), dims300, 3000, {0, 500, 1500},
                              {0, -1, 0}, {0, 0, 1}, ipe200, SectionAnchor::Bottom, params);
        check(r.ok, "C: temas + levha uretildi");
        check(approx(r.placement.plateCenter.z, 1600.0), "C: Bottom anchor -> levha merkezi z=1600");
        double bx0, by0, bz0, bx1, by1, bz1, px0, py0, pz0, px1, py1, pz1;
        solidBBox(beam, bx0, by0, bz0, bx1, by1, bz1);
        solidBBox(r.plate, px0, py0, pz0, px1, py1, pz1);
        check(approx(bz0, 1500.0) && approx(bz1, 1700.0), "C: kiris kesiti z=[1500,1700] (Bottom)");
        check(bz0 >= pz0 - 1e-3 && bz1 <= pz1 + 1e-3, "C: kiris kesiti levha icinde");
    }

    // --- D: 30° dondurulmus kolon. Levha yeni yuzeye dik + kesit merkezinde. -
    {
        const TopoDS_Shape col30 = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 30.0,
                                                       {0, 1, 0});
        MemberLocalFrame rf;
        rf.origin = {0, 0, 0};
        rf.u = {cos30, sin30, 0};
        rf.v = {-sin30, cos30, 0};
        rf.w = {0, 0, 1};
        const Vec3 beamEnd{-sin30 * 500.0, cos30 * 500.0, 1500.0};
        const Vec3 dir{sin30, -cos30, 0};
        const Solve r = solve(col30, rf, dims300, 3000, beamEnd, dir, {0, 0, 1}, ipe200,
                              SectionAnchor::Top, params);
        check(r.ok, "D: 30° kolon temas + levha");
        check(approx(r.contact.outwardNormal.x, -sin30) && approx(r.contact.outwardNormal.y, cos30),
              "D: dis normal dondu");
        // Levha kalinlik yonu = donmus normal; levha merkezi kesit merkezinde
        // (normal yonunde gap+t/2). Kot korunur (anchor ofseti yalniz height yonu).
        check(approx(r.placement.axis.x, -sin30) && approx(r.placement.axis.y, cos30),
              "D: levha ekseni donmus normal");
        // Kesit merkezi: kiris ekseni z=1500 (Top) -> merkez z=1400. Kot korundu.
        check(approx(r.placement.plateCenter.z, 1400.0), "D: levha merkezi kesit merkezinde (kot korundu)");
    }

    // --- E: farkli Z kotu. Kiris z=2500 -> levha merkezi z=2400 (Top anchor). -
    {
        const Solve r = solve(col300, colFrame({0, 0, 0}), dims300, 3000, {0, 500, 2500},
                              {0, -1, 0}, {0, 0, 1}, ipe200, SectionAnchor::Top, params);
        check(r.ok, "E: yuksek kot temas + levha");
        check(approx(r.placement.plateCenter.z, 2400.0), "E: levha merkezi z=2400 (kotu takip etti)");
        check(approx(r.placement.plateCenter.y, 6.0), "E: Y konumu korundu");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
