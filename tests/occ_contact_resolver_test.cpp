// REAL OCCT B-REP CONTACT RESOLVER (Phase R2c) — kabul testleri.
//
// Gercek TopoDS_Shape (I-kesit kati) uzerinde temas yuzu/noktasi cozumu:
// flans dis yuzu, dondurulmus kolon, govde, sinir disi, paralel eksen, delik,
// arka yuz secimi. Yalniz matematiksel duzlem DEGIL — gercek B-Rep kullanilir.

#include "model_maker/occ_contact_resolver.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++failures;                                                      \
            std::printf("FAIL: %s\n", msg);                                  \
        } else {                                                            \
            std::printf("PASS %s\n", msg);                                   \
        }                                                                    \
    } while (0)
static bool approx(double a, double b, double tol = 1e-3) { return std::abs(a - b) < tol; }

int main() {
    const double cos30 = 0.86602540378443864676;
    const double sin30 = 0.5;

    SteelProfile heb300;
    heb300.name = "HEB300";
    heb300.height = 300.0;
    heb300.width = 300.0;
    heb300.plateThickness = 11.0;
    heb300.flangeThickness = 19.0;
    const SectionDims dims{300.0, 300.0, 11.0, 19.0};

    // Dikey kolon: axis Z, genislik X, yukseklik Y. Eksen kesitin USTUNDEN
    // gecer (top-of-section) — ust flans dis yuzu Y=0'da.
    const TopoDS_Shape col = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0, {0, 1, 0});
    const MemberLocalFrame f =
        makeMemberLocalFrame(profileSectionFrame({0, 0, 1}, {0, 1, 0}), {0, 0, 0});

    // --- A: +Y kiris -> ust flans dis yuzu, kot korunur ------------------------
    {
        const auto c = resolveOcctContact(col, f, dims, 3000, {0, 500, 1500}, {0, -1, 0},
                                          SemanticFaceRole::FlangeOuterPositive);
        CHECK(c.status == ResolutionStatus::Resolved, "A: +Y kiris Resolved");
        CHECK(c.semanticFaceRole == SemanticFaceRole::FlangeOuterPositive,
              "A: FlangeOuterPositive (ust flans dis yuzu)");
        CHECK(approx(c.outwardNormal.y, 1.0), "A: outward normal +Y");
        CHECK(approx(c.contactPoint.y, 0.0), "A: temas Y=0 (ust flans dis yuzu)");
        CHECK(approx(c.contactPoint.z, 1500.0), "A: temas KOTU 1500 korunur");
    }

    // --- I: arka yuz (alt flans) yanlislikla secilmiyor ------------------------
    {
        const auto c = resolveOcctContact(col, f, dims, 3000, {0, 500, 1500}, {0, -1, 0},
                                          SemanticFaceRole::FlangeOuterPositive);
        CHECK(c.outwardNormal.y > 0.0, "I: normal +Y (alt flans -Y secilmedi)");
    }

    // --- D: farkli kotlarda baglanti -------------------------------------------
    {
        const auto lo = resolveOcctContact(col, f, dims, 3000, {0, 500, 400}, {0, -1, 0},
                                           SemanticFaceRole::FlangeOuterPositive);
        const auto hi = resolveOcctContact(col, f, dims, 3000, {0, 500, 2600}, {0, -1, 0},
                                           SemanticFaceRole::FlangeOuterPositive);
        CHECK(approx(lo.contactPoint.z, 400.0) && approx(hi.contactPoint.z, 2600.0),
              "D: alt/ust kotta temas koti korunur");
    }

    // --- E: govde yuzune kiris -------------------------------------------------
    {
        // Y=-150 govde yuksekligi (flanslarin arasi); kiris +X'ten gelir.
        const auto c = resolveOcctContact(col, f, dims, 3000, {500, -150, 1500}, {-1, 0, 0},
                                          SemanticFaceRole::WebPositive);
        CHECK(c.status == ResolutionStatus::Resolved &&
                  c.semanticFaceRole == SemanticFaceRole::WebPositive,
              "E: WebPositive (govde yuzu)");
        CHECK(approx(c.contactPoint.x, 5.5), "E: temas X=tw/2=5.5 (b/2 DEGIL)");
    }

    // --- F: kiris ekseni flans genisliginin DISINDA -> reddedilir --------------
    {
        const auto c = resolveOcctContact(col, f, dims, 3000, {200, 500, 1500}, {0, -1, 0},
                                          SemanticFaceRole::FlangeOuterPositive);
        CHECK(c.status == ResolutionStatus::OutsideFace,
              "F: X=200 (flans disi) -> OutsideFace");
    }

    // --- H: kiris ekseni duzleme paralel -> ParallelAxis -----------------------
    {
        const auto c = resolveOcctContact(col, f, dims, 3000, {0, 500, 1500}, {0, 0, 1},
                                          SemanticFaceRole::FlangeOuterPositive);
        CHECK(c.status == ResolutionStatus::ParallelAxis, "H: dusey kiris -> ParallelAxis");
    }

    // --- G: flans yuzunde gercek delik -> delik ici kesim reddedilir ------------
    {
        // Tam gecisli delik: silindir kolonun TAMAMINI keser (ust+alt flans +
        // govde); kiris deligin ekseninden gecer, HICBIR yuze degmez.
        const TopoDS_Shape holed = booleanCutShape(
            col, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, -350, 1500), gp_Dir(0, 1, 0)), 20.0, 400.0)
                     .Shape());
        const auto c = resolveOcctContact(holed, f, dims, 3000, {0, 500, 1500}, {0, -1, 0},
                                          SemanticFaceRole::FlangeOuterPositive);
        CHECK(c.status == ResolutionStatus::OutsideFace,
              "G: delik icindeki kesim reddedildi (OutsideFace)");
    }

    // --- B: 30° dondurulmus kolon — normal uyeyle doner -------------------------
    {
        const TopoDS_Shape col30 = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 30.0, {0, 1, 0});
        MemberLocalFrame r;
        r.origin = {0, 0, 0};
        r.u = {cos30, sin30, 0};
        r.v = {-sin30, cos30, 0};
        r.w = {0, 0, 1};
        // +v yuzune dik yaklasma: beam origin = v*400, dir = -v.
        const Vec3 beamOrigin{-sin30 * 400.0, cos30 * 400.0, 1500.0};
        const Vec3 dir{sin30, -cos30, 0};
        const auto c = resolveOcctContact(col30, r, dims, 3000, beamOrigin, dir,
                                          SemanticFaceRole::FlangeOuterPositive);
        CHECK(c.status == ResolutionStatus::Resolved, "B: 30° kolon Resolved");
        CHECK(c.semanticFaceRole == SemanticFaceRole::FlangeOuterPositive,
              "B: FlangeOuterPositive");
        CHECK(approx(c.outwardNormal.x, -sin30) && approx(c.outwardNormal.y, cos30),
              "B: outward normal dondurulmus (+v = -sin30, cos30)");
        CHECK(approx(c.contactPoint.z, 1500.0), "B: kot korunur");
    }

    // --- C: 45° dondurulmus kolon — dunya bbox yuzuyle karistirilmiyor ----------
    {
        const double c45 = 0.7071067811865475244;
        const TopoDS_Shape col45 = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 45.0, {0, 1, 0});
        MemberLocalFrame r;
        r.origin = {0, 0, 0};
        r.u = {c45, c45, 0};
        r.v = {-c45, c45, 0};
        r.w = {0, 0, 1};
        const Vec3 beamOrigin{-c45 * 400.0, c45 * 400.0, 1500.0};
        const Vec3 dir{c45, -c45, 0};
        const auto c = resolveOcctContact(col45, r, dims, 3000, beamOrigin, dir,
                                          SemanticFaceRole::FlangeOuterPositive);
        CHECK(c.status == ResolutionStatus::Resolved, "C: 45° kolon Resolved");
        CHECK(std::abs(c.outwardNormal.x) > 0.6 && std::abs(c.outwardNormal.y) > 0.6,
              "C: normal dunya eksenine hizalı DEGIL (dondurulmus)");
    }

    // --- R1 (reviewer 1): concav IC yuzun dis normali ----------------------------
    // Flans Cikintisinin altindan (X=100, govde girintisi) ust flansin IC yuzune
    // (Y=-19) +Y yaklasimi: dis normal -Y olmali (asagi, bosluga). Centroid
    // heuristic'i bu yuzun normalini +Y verip rolunu FlangeOuterPositive yapip
    // yanlis yuze dusuyordu.
    {
        const auto c = resolveOcctContact(col, f, dims, 3000, {100, -150, 1500}, {0, 1, 0},
                                          SemanticFaceRole::FlangeOuterNegative);
        CHECK(c.status == ResolutionStatus::Resolved, "R1: IC yuz Resolved");
        CHECK(c.semanticFaceRole == SemanticFaceRole::FlangeOuterNegative,
              "R1: FlangeOuterNegative (ust flans IC yuzu)");
        CHECK(approx(c.outwardNormal.y, -1.0), "R1: IC yuz dis normali -Y");
        CHECK(approx(c.contactPoint.y, -19.0), "R1: temas Y=-19 (ust flans IC yuzu)");
    }

    // --- R2 (reviewer 2): gerideki kesim secilmiyor -----------------------------
    // Kiris KOLONDAN UZAK yonde (+Y, kolon Y∈[-300,0] altinda) isaret ediyor;
    // |t| secimi alt flansi (geride, t<0) "temas" sayiyordu.
    {
        const auto c = resolveOcctContact(col, f, dims, 3000, {0, 200, 1500}, {0, 1, 0},
                                          SemanticFaceRole::FlangeOuterPositive);
        CHECK(c.status == ResolutionStatus::NoContact,
              "R2: gerideki yuz secilmedi (NoContact)");
    }

    // --- R3 (reviewer 3): yakin + uzak aday belirsizlik DEGIL --------------------
    // Capraz kiris ust flansi (Y=0, YAKIN) ve govdeyi (X=5.5, UZAK) keser.
    // Ilk temas (flans) kazanir; uzak govde belirsizlik sayilmamali.
    {
        const auto c = resolveOcctContact(col, f, dims, 3000, {100, 50, 1500},
                                          {-0.7071067811865475, -0.7071067811865475, 0.0},
                                          SemanticFaceRole::None);
        CHECK(c.status == ResolutionStatus::Resolved, "R3: ilk temas Resolved");
        CHECK(c.semanticFaceRole == SemanticFaceRole::FlangeOuterPositive,
              "R3: yakin flans secildi (uzak govde belirsizlik degil)");
        CHECK(approx(c.outwardNormal.y, 1.0), "R3: outward normal +Y");
    }

    // --- R4 (reviewer onerisi): non-finite giris reddi ---------------------------
    {
        const double nan = std::nan("");
        const auto c = resolveOcctContact(col, f, dims, 3000, {0, nan, 1500}, {0, -1, 0},
                                          SemanticFaceRole::FlangeOuterPositive);
        CHECK(c.status == ResolutionStatus::Unresolved, "R4: NaN giris Unresolved");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
