// GEOMETRY REFERENCE RESOLVER + MAGNETIC FRAME (Phase R2d-1) — kabul testleri.
//
// 1. BasicGeometryReferenceResolver: MemberFrame / MemberStart / MemberEnd /
//    SemanticFace (I-kesit ±h/2, ±tw/2, ±b/2) / ConstructionPlane /
//    ComponentPlane cozumu; kayip kaynak, eksik duzlem, gecersiz rol icin
//    fail-closed UNRESOLVED; dondurulmus cerceve normali uyeyle doner.
// 2. MagneticFrame: lokal vektor/nokta/cerceve -> dunya; cerceve tasininca alt
//    nesnelerin dunya koordinatlari tutarli yeniden turetir; sag-ellilik korunur.

#include "model_maker/geometry_reference_resolver.hpp"
#include "model_maker/magnetic_frame.hpp"

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
static bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) < tol; }
static bool nearVec(const Vec3& a, const Vec3& b, double tol = 1e-9) {
    return near(a.x, b.x, tol) && near(a.y, b.y, tol) && near(a.z, b.z, tol);
}

int main() {
    const double cos30 = 0.86602540378443864676;
    const double sin30 = 0.5;

    // HEB300 kolon: identity cerceve (u=+X, v=+Y, w=+Z), origin {0,0,0}.
    const SectionDims dims{300.0, 300.0, 11.0, 19.0};
    const MemberLocalFrame colFrame =
        makeMemberLocalFrame(profileSectionFrame({0, 0, 1}, {0, 1, 0}), {0, 0, 0});
    BasicGeometryReferenceResolver::MemberMap members;
    members[1] = {colFrame, dims, 3000.0};

    BasicGeometryReferenceResolver resolver(std::move(members));

    // --- MemberFrame ---
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::MemberFrame;
        r.source = 1;
        const auto g = resolver.resolve(r);
        CHECK(g.resolved && g.status == ResolutionStatus::Resolved, "R1: MemberFrame Resolved");
        CHECK(nearVec(g.frame.origin, colFrame.origin), "R1: frame origin");
        CHECK(nearVec(g.frame.u, {1, 0, 0}) && nearVec(g.frame.v, {0, 1, 0}) &&
                  nearVec(g.frame.w, {0, 0, 1}),
              "R1: frame eksenleri");
    }

    // --- MemberStart (normal -w) ---
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::MemberStart;
        r.source = 1;
        const auto g = resolver.resolve(r);
        CHECK(g.resolved, "R2: MemberStart Resolved");
        CHECK(nearVec(g.plane.origin, {0, 0, 0}), "R2: start origin {0,0,0}");
        CHECK(nearVec(g.plane.normal, {0, 0, -1}), "R2: start normal -w");
    }

    // --- MemberEnd (origin + length*w, normal +w) ---
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::MemberEnd;
        r.source = 1;
        const auto g = resolver.resolve(r);
        CHECK(g.resolved, "R3: MemberEnd Resolved");
        CHECK(nearVec(g.plane.origin, {0, 0, 3000}), "R3: end origin {0,0,3000}");
        CHECK(nearVec(g.plane.normal, {0, 0, 1}), "R3: end normal +w");
    }

    // --- SemanticFace FlangeOuterPositive (+h/2 boyunca v) ---
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::SemanticFace;
        r.source = 1;
        r.faceRole = SemanticFaceRole::FlangeOuterPositive;
        const auto g = resolver.resolve(r);
        CHECK(g.resolved, "R4: FlangeOuterPositive Resolved");
        CHECK(nearVec(g.plane.origin, {0, 150, 0}), "R4: origin +v*(h/2)={0,150,0}");
        CHECK(nearVec(g.plane.normal, {0, 1, 0}), "R4: normal +v");
    }

    // --- SemanticFace WebPositive (+tw/2 boyunca u) ---
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::SemanticFace;
        r.source = 1;
        r.faceRole = SemanticFaceRole::WebPositive;
        const auto g = resolver.resolve(r);
        CHECK(g.resolved, "R5: WebPositive Resolved");
        CHECK(nearVec(g.plane.origin, {5.5, 0, 0}), "R5: origin +u*(tw/2)={5.5,0,0}");
        CHECK(nearVec(g.plane.normal, {1, 0, 0}), "R5: normal +u");
    }

    // --- SemanticFace FlangeEdgeNegative (-b/2 boyunca u) ---
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::SemanticFace;
        r.source = 1;
        r.faceRole = SemanticFaceRole::FlangeEdgeNegative;
        const auto g = resolver.resolve(r);
        CHECK(g.resolved, "R6: FlangeEdgeNegative Resolved");
        CHECK(nearVec(g.plane.origin, {-150, 0, 0}), "R6: origin -u*(b/2)={-150,0,0}");
        CHECK(nearVec(g.plane.normal, {-1, 0, 0}), "R6: normal -u");
    }

    // --- ConstructionPlane (explicit; kaynak EntityId'si gecerli) ---
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::ConstructionPlane;
        r.source = 100;  // duzlem varligi kimligi
        r.explicitPlane = ReferencePlane{{1, 2, 3}, {0, 0, 1}};
        const auto g = resolver.resolve(r);
        CHECK(g.resolved, "R7: ConstructionPlane Resolved");
        CHECK(nearVec(g.plane.origin, {1, 2, 3}) && nearVec(g.plane.normal, {0, 0, 1}),
              "R7: explicit duzlem aynen");
    }

    // --- Fail-closed: kayip kaynak ---
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::MemberFrame;
        r.source = 999;
        const auto g = resolver.resolve(r);
        CHECK(!g.resolved && g.status == ResolutionStatus::Unresolved,
              "R8: kayip kaynak UNRESOLVED");
    }

    // --- Fail-closed: ComponentPlane duzlemsiz ---
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::ComponentPlane;
        const auto g = resolver.resolve(r);
        CHECK(!g.resolved, "R9: ComponentPlane duzlemsiz UNRESOLVED");
    }

    // --- Fail-closed: None kind ---
    {
        GeometryReference r;  // kind = None
        const auto g = resolver.resolve(r);
        CHECK(!g.resolved, "R10: None kind UNRESOLVED");
    }

    // --- Fail-closed: SemanticFace faceRole None ---
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::SemanticFace;
        r.source = 1;  // faceRole = None
        const auto g = resolver.resolve(r);
        CHECK(!g.resolved, "R11: SemanticFace faceRole=None UNRESOLVED");
    }

    // --- Dondurulmus cerceve (30°): normal uyeyle doner ---
    {
        MemberLocalFrame rot;
        rot.origin = {0, 0, 0};
        rot.u = {cos30, sin30, 0};
        rot.v = {-sin30, cos30, 0};
        rot.w = {0, 0, 1};
        BasicGeometryReferenceResolver::MemberMap m2;
        m2[2] = {rot, dims, 3000.0};
        BasicGeometryReferenceResolver r2(std::move(m2));
        GeometryReference r;
        r.kind = GeometryReferenceKind::SemanticFace;
        r.source = 2;
        r.faceRole = SemanticFaceRole::FlangeOuterPositive;
        const auto g = r2.resolve(r);
        CHECK(g.resolved, "R12: 30° FlangeOuterPositive Resolved");
        CHECK(nearVec(g.plane.normal, {-sin30, cos30, 0}),
              "R12: normal dondurulmus +v = {-0.5, 0.866, 0}");
        CHECK(nearVec(g.plane.origin, {-sin30 * 150.0, cos30 * 150.0, 0}),
              "R12: origin dondurulmus +v*(h/2)");
    }

    // --- SemanticFace localFrame override: kayit identity, ref 30° cerceve ---
    {
        BasicGeometryReferenceResolver::MemberMap m3;
        m3[3] = {colFrame, dims, 3000.0};  // identity kayit
        BasicGeometryReferenceResolver r3(std::move(m3));
        MemberLocalFrame rot;
        rot.origin = {0, 0, 0};
        rot.u = {cos30, sin30, 0};
        rot.v = {-sin30, cos30, 0};
        rot.w = {0, 0, 1};
        GeometryReference r;
        r.kind = GeometryReferenceKind::SemanticFace;
        r.source = 3;
        r.faceRole = SemanticFaceRole::FlangeOuterPositive;
        r.localFrame = rot;  // override
        const auto g = r3.resolve(r);
        CHECK(g.resolved, "R13: localFrame override Resolved");
        CHECK(nearVec(g.plane.normal, {-sin30, cos30, 0}),
              "R13: override cerceve normali kullanilir");
    }

    // --- R14 (reviewer 1): kayip kaynakli ConstructionPlane UNRESOLVED ---------
    // explicitPlane yalnizca runtime cache; KIMLIK kaynak EntityId'sidir. Kaynak
    // yoksa referans KAYIPTIR — duzlem degeri mevcut olsa bile cozulmez.
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::ConstructionPlane;
        r.explicitPlane = ReferencePlane{{1, 2, 3}, {0, 0, 1}};  // source=0
        const auto g = resolver.resolve(r);
        CHECK(!g.resolved, "R14: kayip kaynakli duzlem UNRESOLVED");
    }

    // --- R15 (reviewer 2): dejenere (sifir) normal reddedilir ------------------
    {
        GeometryReference r;
        r.kind = GeometryReferenceKind::ConstructionPlane;
        r.source = 100;
        r.explicitPlane = ReferencePlane{{1, 2, 3}, {0, 0, 0}};  // sifir normal
        const auto g = resolver.resolve(r);
        CHECK(!g.resolved, "R15: sifir normal UNRESOLVED");
    }

    // --- R16 (reviewer 2): ortonormal olmayan localFrame override reddedilir ---
    {
        BasicGeometryReferenceResolver::MemberMap m4;
        m4[4] = {colFrame, dims, 3000.0};  // identity kayit
        BasicGeometryReferenceResolver r4(std::move(m4));
        MemberLocalFrame bad;
        bad.origin = {0, 0, 0};
        bad.u = {2, 0, 0};  // birim DEGIL
        bad.v = {0, 1, 0};
        bad.w = {0, 0, 1};
        GeometryReference r;
        r.kind = GeometryReferenceKind::SemanticFace;
        r.source = 4;
        r.faceRole = SemanticFaceRole::FlangeOuterPositive;
        r.localFrame = bad;  // dejenere override
        const auto g = r4.resolve(r);
        CHECK(!g.resolved, "R16: ortonormal olmayan cerceve UNRESOLVED");
    }

    // =========================================================================
    // MAGNETIC FRAME
    // =========================================================================

    // --- transformPoint: identity frame + translation ---
    {
        const MemberLocalFrame f{{10, 20, 30}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const Vec3 w = magneticTransformPoint(f, {1, 2, 3});
        CHECK(nearVec(w, {11, 22, 33}), "M1: lokal nokta {1,2,3} -> {11,22,33}");
    }

    // --- transformVec: 90° Z rotasyonu (X -> Y) ---
    {
        const MemberLocalFrame f{{0, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, 0, 1}};
        const Vec3 w = magneticTransformVec(f, {1, 0, 0});
        CHECK(nearVec(w, {0, 1, 0}), "M2: 90° Z rotasyonu X->Y");
    }

    // --- transformFrame: child cerceve dunyaya tasinir ---
    {
        const MemberLocalFrame parent{{0, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, 0, 1}};
        const MemberLocalFrame childLocal{{0, 0, 5}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const MemberLocalFrame w = magneticTransformFrame(parent, childLocal);
        CHECK(nearVec(w.origin, {0, 0, 5}), "M3: child origin {0,0,5} korunur (w=Z)");
        CHECK(nearVec(w.u, {0, 1, 0}), "M3: child u doner -> +Y");
        CHECK(nearVec(w.v, {-1, 0, 0}), "M3: child v doner -> -X");
        CHECK(isOrthonormalRightHanded(w), "M3: sonuc sag-elli ortonormal");
    }

    // --- computeChildWorldFrames: cerceve tasinir, alt nesneler takip eder ---
    {
        ComponentLocalFrame clf;
        clf.frame = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        clf.children.push_back({"Plate", {{0, 0, 6}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}});
        clf.children.push_back({"BoltGroup", {{30, 40, 6}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}});

        const auto before = computeChildWorldFrames(clf);
        CHECK(before.size() == 2, "M4: 2 alt nesne turetildi");
        CHECK(nearVec(before[0].second.origin, {0, 0, 6}), "M4: Plate once {0,0,6}");
        CHECK(nearVec(before[1].second.origin, {30, 40, 6}), "M4: BoltGroup once {30,40,6}");

        // MagneticFrame TASINIR: origin {100, 50, 0}'a gider.
        clf.frame.origin = {100, 50, 0};
        const auto after = computeChildWorldFrames(clf);
        CHECK(nearVec(after[0].second.origin, {100, 50, 6}),
              "M4: Plate takip eder -> {100,50,6}");
        CHECK(nearVec(after[1].second.origin, {130, 90, 6}),
              "M4: BoltGroup takip eder -> {130,90,6}");
        // Eksenler (identity local) tasimadan etkilenmez.
        CHECK(nearVec(after[0].second.u, {1, 0, 0}), "M4: Plate eksenleri korunur");
    }

    // --- Cerceve hem dondurulup hem tasinirsa alt nesneler tutarli ---
    {
        ComponentLocalFrame clf;
        clf.frame = {{10, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, 0, 1}};  // 90° Z + X=10
        clf.children.push_back({"Plate", {{5, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}});
        const auto ws = computeChildWorldFrames(clf);
        // lokal {5,0,0} -> basis {0,1,0}*5 = {0,5,0}; + origin {10,0,0} = {10,5,0}.
        CHECK(nearVec(ws[0].second.origin, {10, 5, 0}),
              "M5: donme+otasimada child origin {10,5,0}");
        CHECK(nearVec(ws[0].second.u, {0, 1, 0}), "M5: child u doner +Y");
        CHECK(isOrthonormalRightHanded(ws[0].second), "M5: sag-ellilik korunur");
    }

    // --- M6 (reviewer 3): dejenere magnetic cerceve -> bos (fail-closed) -------
    {
        ComponentLocalFrame clf;
        clf.frame = {{0, 0, 0}, {2, 0, 0}, {0, 1, 0}, {0, 0, 1}};  // u birim DEGIL
        clf.children.push_back({"Plate", {{0, 0, 6}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}});
        const auto ws = computeChildWorldFrames(clf);
        CHECK(ws.empty(), "M6: dejenere cerceve bos doner (copluk uretilmez)");
        CHECK(!isValidComponentLocalFrame(clf), "M6: isValidComponentLocalFrame=false");
    }

    // --- M7 (reviewer 3): rijit olmayan alt nesne atlanir ----------------------
    {
        ComponentLocalFrame clf;
        clf.frame = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        clf.children.push_back({"Plate", {{0, 0, 6}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}});
        clf.children.push_back({"BadBolt", {{9, 9, 9}, {2, 0, 0}, {0, 1, 0}, {0, 0, 1}}});
        const auto ws = computeChildWorldFrames(clf);
        CHECK(ws.size() == 1 && ws[0].first == "Plate",
              "M7: rijit olmayan alt nesne atlandi, gecerli kaldi");
        CHECK(!isValidComponentLocalFrame(clf), "M7: isValidComponentLocalFrame=false");
    }

    // --- M8 (nokta/vektor/normal ayrimi): normal basis ile doner ---------------
    {
        // 90° Z rotasyonu: dunya X -> Y. Yuz normali +X (dunya) lokalde +Y'ye
        // karsilik gelir; normal, nokta gibi origin TASIMAZ, vektor gibi doner.
        const MemberLocalFrame f{{100, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, 0, 1}};
        const Vec3 normalLocal = {1, 0, 0};   // lokal +X
        const Vec3 nWorld = magneticTransformVec(f, normalLocal);
        CHECK(nearVec(nWorld, {0, 1, 0}), "M8: normal basis ile doner (origin yok)");
        // Nokta ise origin TASIR: {1,0,0} noktasi origin+basis = {100,0,0}+{0,1,0}.
        const Vec3 pWorld = magneticTransformPoint(f, {1, 0, 0});
        CHECK(nearVec(pWorld, {100, 1, 0}), "M8: nokta origin tasir");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
