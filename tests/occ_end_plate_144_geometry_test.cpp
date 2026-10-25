// TEKLA 144 — MILESTONE 1 GEOMETRY CORRECTNESS KABUL TESTI (gercek OCCT).
//
// Yeni davranislari kilitler:
//   E1. Web tarafi temas (kiris kolon GOVDESINE yaklasir): preferredRole=None
//       ile dogru web yuzu (WebPositive, normal +X) cozulur. Eski sabit
//       FlangeOuterPositive tercihi bunu NoContact'a dusuruyordu.
//   E2. Negatif flans tarafi: None ile FlangeOuterNegative (normal -Y) dogru.
//   D.  Profile rotation: kiris kesiti 90° donukken levha cercevesi (u/v)
//       gercek kesit yonune oturur (yanlis 90° donme yok); levha B-Rep'i
//       kiris kesitini sarar.
//
// Derleme (Linux + OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_end_plate_144_geometry_test.cpp \
//       src/end_plate.cpp src/end_plate_geometry.cpp src/bolt.cpp \
//       src/bolt_geometry.cpp src/geometry.cpp src/profile_database.cpp \
//       src/reference_geometry.cpp src/contact_resolver.cpp \
//       src/occ_contact_resolver.cpp src/occ_geometry.cpp src/cut_modifier.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh \
//       -lTKXSBase -lTKDESTEP \
//       -o occ_end_plate_144_geometry_test && ./occ_end_plate_144_geometry_test

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

int main() {
    const SteelProfile heb300 = prof("HEB300", 300.0, 300.0, 11.0, 19.0);
    const SteelProfile ipe200 = prof("IPE200", 200.0, 100.0, 5.6, 8.5);
    const SectionDims dims300{300.0, 300.0, 11.0, 19.0};

    EndPlateParams params;  // M20/22, 12mm levha, 2x4 bulon, gap=0
    params.gap = 0.0;
    params.boltCountX = 2;
    params.boltCountY = 4;

    // Dik kolon: axis +Z, ref normal +Y -> genislik +X, yukseklik +Y.
    //   Flans dis yuzleri: normal ±Y. Govde yuzleri: normal ±X.
    const auto colFrame = makeMemberLocalFrame(profileSectionFrame({0, 0, 1}, {0, 1, 0}),
                                               Vec3{0, 0, 0});
    const TopoDS_Shape col300 = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0,
                                                    {0, 1, 0});

    // --- E1: WEB tarafi. Kiris +X'ten gelir (beamDir -X), GOVDE ortasi ---------
    // y=-150 (Top anchor kesiti y∈[-300,0] yapar; govde ortasi -150). Govde yuzu
    // (normal +X) temas etmeli; eski sabit FlangeOuterPositive bunu NoContact'a
    // dusururdu. Flans kenar yuzleri (x=±150) SONLU yuzdur, y=-150'de yoktur.
    {
        const Vec3 beamEnd{500.0, -150.0, 1500.0};
        const Vec3 dir{-1.0, 0.0, 0.0};
        const ContactPlacement c = resolveOcctContact(col300, colFrame, dims300, 3000,
                                                      beamEnd, dir, SemanticFaceRole::None);
        check(c.status == ResolutionStatus::Resolved, "E1: web temas Resolved");
        check(approx(c.outwardNormal.x, 1.0) && approx(c.outwardNormal.y, 0.0) &&
                  approx(c.outwardNormal.z, 0.0),
              "E1: dis normal +X (govde)");
        check(c.semanticFaceRole == SemanticFaceRole::WebPositive, "E1: rol WebPositive");
        check(approx(c.contactPoint.x, 5.5) && approx(c.contactPoint.y, -150.0) &&
                  approx(c.contactPoint.z, 1500.0),
              "E1: temas noktasi govde uzerinde x=5.5");
    }

    // --- E2: NEGATIF flans. Kiris -Y'den gelir (beamDir +Y). -----------------
    // Top anchor: alt flans dis yuzu y=-300 (normal -Y).
    {
        const Vec3 beamEnd{0.0, -500.0, 1500.0};
        const Vec3 dir{0.0, 1.0, 0.0};
        const ContactPlacement c = resolveOcctContact(col300, colFrame, dims300, 3000,
                                                      beamEnd, dir, SemanticFaceRole::None);
        check(c.status == ResolutionStatus::Resolved, "E2: negatif flans Resolved");
        check(approx(c.outwardNormal.y, -1.0), "E2: dis normal -Y");
        check(c.semanticFaceRole == SemanticFaceRole::FlangeOuterNegative,
              "E2: rol FlangeOuterNegative");
        check(approx(c.contactPoint.y, -300.0) && approx(c.contactPoint.z, 1500.0),
              "E2: temas noktasi -Y flans yuzu (y=-300)");
    }

    // --- D: PROFILE ROTATION 90°. Kiris +Y'den gelir (beamDir -Y), -----------
    // kesit normali Z, profileRotation=90°. Donsuz kesit: genislik +X,
    // yukseklik +Z. 90° donme sonrasi: genislik +Z, yukseklik -X. Levha dikey
    // ekseni (v) -X'e, yatay ekseni (u) -Z'ye oturmali (dunya Z'ye SABIT degil).
    {
        const Vec3 beamDir{0.0, -1.0, 0.0};
        const double rotDeg = 90.0;
        // resolveEndPlatePlacement ile AYNI: cerceve + profileRotation.
        const ProfileSectionFrame fs = profileSectionFrame(beamDir, {0, 0, 1});
        const double a = rotDeg * 0.017453292519943295;
        const double ca = std::cos(a), sa = std::sin(a);
        const Vec3 w = fs.width, h = fs.height;
        const Vec3 secW{w.x * ca + h.x * sa, w.y * ca + h.y * sa, w.z * ca + h.z * sa};
        const Vec3 secH{h.x * ca - w.x * sa, h.y * ca - w.y * sa, h.z * ca - w.z * sa};
        // Donsuz: w=+X, h=+Z. 90°: secW=+Z, secH=-X. Dogrulama.
        check(approx(secH.x, -1.0) && approx(secH.z, 0.0, 1e-3), "D: secH -X (90° donme)");

        const ContactPlacement c = resolveOcctContact(col300, colFrame, dims300, 3000,
                                                      {0, 500, 1500}, beamDir,
                                                      SemanticFaceRole::None);
        check(c.status == ResolutionStatus::Resolved, "D: temas Resolved");
        ReferencePlane face;
        face.origin = c.contactPoint;
        face.normal = c.outwardNormal;
        const double anchorOff = profileSectionAnchorOffset(ipe200, SectionAnchor::Top);
        const EndPlatePlacement pl = computeEndPlatePlacementOnFace(
            face, params.gap, ipe200.width, ipe200.height, params, secH, anchorOff, secW);
        // Levha normali +Y (kolon flansi); dikey eksen -X; yatay eksen -Z.
        check(approx(pl.axis.y, 1.0), "D: levha normali +Y");
        check(approx(pl.v.x, -1.0) && approx(pl.v.z, 0.0, 1e-3), "D: levha dikey -X (kesit takip)");
        check(approx(pl.u.z, -1.0) && approx(pl.u.x, 0.0, 1e-3), "D: levha yatay -Z");

        // Levha B-Rep'i, 90° donuk kiris kesitini SARMALI (kesit YUKSEKLIGI X
        // yonunde oldu: kiris bbox x-yayilimi 200, z-yayilimi 100).
        const TopoDS_Shape beam = extrudeProfileSolid(ipe200, {0, 1000, 1500}, {0, 60, 1500},
                                                      rotDeg, {0, 0, 1}, SectionAnchor::Top);
        const TopoDS_Shape plate = buildEndPlateFromPlacement(pl);
        check(!plate.IsNull(), "D: levha uretildi");
        double bx0, by0, bz0, bx1, by1, bz1, px0, py0, pz0, px1, py1, pz1;
        solidBBox(beam, bx0, by0, bz0, bx1, by1, bz1);
        solidBBox(plate, px0, py0, pz0, px1, py1, pz1);
        // Kesit x-yayilimi ~200 (yukseklik), z-yayilimi ~100 (genislik).
        check(approx(bx1 - bx0, 200.0, 1.0) && approx(bz1 - bz0, 100.0, 1.0),
              "D: donuk kiris kesiti x=200 z=100");
        check(bx0 >= px0 - 1e-2 && bx1 <= px1 + 1e-2, "D: kesit levha X icinde");
        check(bz0 >= pz0 - 1e-2 && bz1 <= pz1 + 1e-2, "D: kesit levha Z icinde");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
