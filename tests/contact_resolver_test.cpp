// CONTACT RESOLVER (Phase R2b) — MemberLocalFrame + SemanticFaceResolver +
// ContactPointResolver unit testleri. Dondurulmus kolon, temas noktasi (kot),
// sonlu yuz sinirlari, paralel eksen, ters normal. OCC'siz, Linux'ta kosar.

#include "model_maker/contact_resolver.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int g_failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++g_failures;
}
static bool approx(double a, double b, double tol = 1e-9) { return std::abs(a - b) < tol; }

int main() {
    const double cos30 = 0.86602540378443864676;
    const double sin30 = 0.5;

    // --- 1) makeMemberLocalFrame + ortonormal sag elli dogrulama ---------------
    {
        const ProfileSectionFrame s{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const MemberLocalFrame f = makeMemberLocalFrame(s, {0, 0, 0});
        check(approx(f.u.x, 1.0) && approx(f.v.y, 1.0) && approx(f.w.z, 1.0),
              "makeMemberLocalFrame: u/v/w atandi");
        check(isOrthonormalRightHanded(f), "birim cerceve ortonormal sag elli");

        MemberLocalFrame r;
        r.origin = {0, 0, 0};
        r.u = {cos30, sin30, 0};
        r.v = {-sin30, cos30, 0};
        r.w = {0, 0, 1};
        check(isOrthonormalRightHanded(r), "30° cerceve ortonormal sag elli");
    }

    // --- 2) resolveSemanticFacePlane: I-kesit ofsetleri ------------------------
    {
        MemberLocalFrame f;
        f.origin = {0, 0, 0};
        f.u = {1, 0, 0};
        f.v = {0, 1, 0};
        f.w = {0, 0, 1};
        const SectionDims d{300, 300, 11, 19};

        const auto fp = resolveSemanticFacePlane(f, d, 3000, SemanticFaceRole::FlangeOuterPositive);
        check(fp && approx(fp->normal.y, 1.0) && approx(fp->origin.y, 150.0),
              "FlangeOuterPositive: +h/2=150, normal +v");
        const auto fn = resolveSemanticFacePlane(f, d, 3000, SemanticFaceRole::FlangeOuterNegative);
        check(fn && approx(fn->normal.y, -1.0) && approx(fn->origin.y, -150.0),
              "FlangeOuterNegative: -h/2=-150, normal -v");
        const auto wp = resolveSemanticFacePlane(f, d, 3000, SemanticFaceRole::WebPositive);
        check(wp && approx(wp->normal.x, 1.0) && approx(wp->origin.x, 5.5),
              "WebPositive: +tw/2=5.5, normal +u (b/2-tw/2 DEGIL)");
        const auto we = resolveSemanticFacePlane(f, d, 3000, SemanticFaceRole::FlangeEdgePositive);
        check(we && approx(we->normal.x, 1.0) && approx(we->origin.x, 150.0),
              "FlangeEdgePositive: +b/2=150, normal +u");
        const auto me = resolveSemanticFacePlane(f, d, 3000, SemanticFaceRole::MemberEnd);
        check(me && approx(me->normal.z, 1.0) && approx(me->origin.z, 3000.0),
              "MemberEnd: z=3000, normal +w");
    }

    // --- 3) +Y kiris -> FlangeOuterPositive, KOT korunur (merkez degil) ---------
    {
        MemberLocalFrame f;
        f.origin = {0, 0, 0};
        f.u = {1, 0, 0};
        f.v = {0, 1, 0};
        f.w = {0, 0, 1};
        const SectionDims d{300, 300, 11, 19};
        const auto c = resolveContactPlacement(f, d, 3000, {0, 500, 2000}, {0, -1, 0},
                                               SemanticFaceRole::FlangeOuterPositive);
        check(c.status == ResolutionStatus::Resolved, "+Y kiris: Resolved");
        check(c.semanticFaceRole == SemanticFaceRole::FlangeOuterPositive,
              "+Y kiris: FlangeOuterPositive");
        check(approx(c.outwardNormal.y, 1.0), "+Y kiris: outward normal +Y");
        check(approx(c.contactPoint.y, 150.0), "+Y kiris: temas y=150 (flans yuzu)");
        check(approx(c.contactPoint.z, 2000.0), "+Y kiris: temas KOTU 2000 korunur (yuz merkezi degil)");
    }

    // --- 4) -Y kiris -> FlangeOuterNegative (ters taraf) ------------------------
    {
        MemberLocalFrame f;
        f.origin = {0, 0, 0};
        f.u = {1, 0, 0};
        f.v = {0, 1, 0};
        f.w = {0, 0, 1};
        const SectionDims d{300, 300, 11, 19};
        const auto c = resolveContactPlacement(f, d, 3000, {0, -500, 1500}, {0, 1, 0},
                                               SemanticFaceRole::FlangeOuterPositive);
        check(c.status == ResolutionStatus::Resolved && 
                  c.semanticFaceRole == SemanticFaceRole::FlangeOuterNegative,
              "-Y kiris: FlangeOuterNegative");
        check(approx(c.outwardNormal.y, -1.0), "-Y kiris: outward normal -Y");
        check(approx(c.contactPoint.y, -150.0), "-Y kiris: temas y=-150");
    }

    // --- 5) Kiris kotu kolon ustunun ustunde -> OutsideFace ---------------------
    {
        MemberLocalFrame f;
        f.origin = {0, 0, 0};
        f.u = {1, 0, 0};
        f.v = {0, 1, 0};
        f.w = {0, 0, 1};
        const SectionDims d{300, 300, 11, 19};
        const auto c = resolveContactPlacement(f, d, 3000, {0, 500, 4000}, {0, -1, 0},
                                               SemanticFaceRole::FlangeOuterPositive);
        check(c.status == ResolutionStatus::OutsideFace, "kot 4000 > 3000: OutsideFace");
    }

    // --- 6) Dusey kiris (eksen duzleme paralel) -> ParallelAxis ------------------
    {
        MemberLocalFrame f;
        f.origin = {0, 0, 0};
        f.u = {1, 0, 0};
        f.v = {0, 1, 0};
        f.w = {0, 0, 1};
        const SectionDims d{300, 300, 11, 19};
        const auto c = resolveContactPlacement(f, d, 3000, {0, 500, 1500}, {0, 0, 1},
                                               SemanticFaceRole::FlangeOuterPositive);
        check(c.status == ResolutionStatus::ParallelAxis, "dusey kiris: ParallelAxis");
    }

    // --- 7) 30° dondurulmus kolon: normal uyeyle doner ---------------------------
    {
        MemberLocalFrame r;
        r.origin = {0, 0, 0};
        r.u = {cos30, sin30, 0};
        r.v = {-sin30, cos30, 0};
        r.w = {0, 0, 1};
        const SectionDims d{300, 300, 11, 19};
        // -v yuzu +X'e bakar; outward normal = -v = (sin30, -cos30, 0).
        const Vec3 nOut{sin30, -cos30, 0};
        const Vec3 beamOrigin{nOut.x * 400.0, nOut.y * 400.0, 1500.0};
        const Vec3 dir{-nOut.x, -nOut.y, 0.0};
        const auto c = resolveContactPlacement(r, d, 3000, beamOrigin, dir,
                                               SemanticFaceRole::FlangeOuterPositive);
        check(c.status == ResolutionStatus::Resolved, "30° kolon: Resolved");
        check(c.semanticFaceRole == SemanticFaceRole::FlangeOuterNegative,
              "30° kolon: FlangeOuterNegative");
        check(approx(c.outwardNormal.x, sin30) && approx(c.outwardNormal.y, -cos30),
              "30° kolon: outward normal dondurulmus (-v)");
        check(approx(c.contactPoint.z, 1500.0), "30° kolon: kot korunur");
        const double dist = (c.contactPoint.x - nOut.x * 150.0) * nOut.x +
                            (c.contactPoint.y - nOut.y * 150.0) * nOut.y;
        check(approx(dist, 0.0, 1e-6), "30° kolon: temas noktasi yuz duzleminde");
    }

    // --- 8) Web yuzu (kiris govdeye baglanir) ------------------------------------
    {
        MemberLocalFrame f;
        f.origin = {0, 0, 0};
        f.u = {1, 0, 0};
        f.v = {0, 1, 0};
        f.w = {0, 0, 1};
        const SectionDims d{300, 300, 11, 19};
        const auto c = resolveContactPlacement(f, d, 3000, {500, 0, 1500}, {-1, 0, 0},
                                               SemanticFaceRole::WebPositive);
        check(c.status == ResolutionStatus::Resolved &&
                  c.semanticFaceRole == SemanticFaceRole::WebPositive,
              "Web kiris: WebPositive");
        check(approx(c.contactPoint.x, 5.5), "Web kiris: temas x=tw/2=5.5 (b/2-tw/2 DEGIL)");
    }

    std::printf("FAILURES: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
