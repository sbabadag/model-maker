// ============================================================================
// ST-3g — KANONIK UYE GEOMETRISI TESTI
//
// Sozlesme kurali: ayni FIZIKSEL uye, cagiranin hangi ucdan basladigina
// bakilmaksizin AYNI MemberGeometry'yi ve AYNI planDigest'i vermeli. Studio
// ST-3g bulgusu: axisTo = baglanti ucu + profileRotation 180 ile gelen girdi
// ayni geometriyi uretiyor ama u/v ve bulon xDir ters donuyor, digest farkli
// cikiyordu (0/5).
// ============================================================================
#include "model_maker/component_engine.hpp"
#include "model_maker/component_codec.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: %s\n", msg);                                    \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static bool sameVec(const Vec3& a, const Vec3& b, double eps = 1e-12) {
    return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps &&
           std::fabs(a.z - b.z) <= eps;
}

static bool sameGeometry(const MemberGeometry& a, const MemberGeometry& b) {
    return sameVec(a.axisFrom, b.axisFrom) && sameVec(a.axisTo, b.axisTo) &&
           sameVec(a.refNormal, b.refNormal) &&
           std::fabs(a.secWidth - b.secWidth) < 1e-12 &&
           std::fabs(a.secHeight - b.secHeight) < 1e-12 &&
           std::fabs(a.secWebThickness - b.secWebThickness) < 1e-12 &&
           std::fabs(a.secFlangeThickness - b.secFlangeThickness) < 1e-12 &&
           std::fabs(a.sectionCenterOffset - b.sectionCenterOffset) < 1e-12 &&
           std::fabs(a.profileRotation - b.profileRotation) < 1e-12;
}

int main() {
    const MemberSectionSpec hea240{240.0, 230.0, 7.5, 12.0, -115.0};  // Top anchor
    // Kolon: 0,0,0 -> 0,0,3000 ; baglanti ucu UST (0,0,3000)
    const Vec3 bottom{0.0, 0.0, 0.0};
    const Vec3 top{0.0, 0.0, 3000.0};

    // ---- 1) UCTAN BAGIMSIZLIK -------------------------------------------
    // Ayni uye iki farkli ucdan yazilir; baglanti ucu ikisinde de `top`.
    const MemberGeometry forward =
        memberGeometryFromProfile(bottom, top, /*connectionAtA=*/false,
                                  Vec3{1.0, 0.0, 0.0}, 0.0, hea240);
    const MemberGeometry reverse =
        memberGeometryFromProfile(top, bottom, /*connectionAtA=*/true,
                                  Vec3{1.0, 0.0, 0.0}, 0.0, hea240);
    CHECK(sameGeometry(forward, reverse), "uc sirasi ayni MemberGeometry verir");
    CHECK(sameVec(forward.axisTo, top), "axisTo = baglanti ucu (top)");
    CHECK(sameVec(forward.axisFrom, bottom), "axisFrom = uzak uc (bottom)");
    CHECK(memberGeometryUsable(forward), "geometri kullanilabilir");

    // ---- 2) refNormal ISARETI -------------------------------------------------
    // Ayni fiziksel normal n ve -n olarak verilebilir; kesit cercevesi ayni.
    const MemberGeometry nPlus =
        memberGeometryFromProfile(top, bottom, true, Vec3{1.0, 0.0, 0.0}, 0.0, hea240);
    const MemberGeometry nMinus =
        memberGeometryFromProfile(top, bottom, true, Vec3{-1.0, 0.0, 0.0}, 0.0, hea240);
    CHECK(sameGeometry(nPlus, nMinus), "refNormal isareti sonucu degistirmez");
    CHECK(nPlus.refNormal.x > 0.0, "refNormal isareti kanonik (ilk bilesen +)");

    // Egik normal de diklestirilir: (1,0,0.5) ile (1,0,0) ayni duzlemi verir.
    const MemberGeometry tilted =
        memberGeometryFromProfile(top, bottom, true, Vec3{1.0, 0.0, 0.5}, 0.0, hea240);
    CHECK(sameGeometry(nPlus, tilted), "refNormal eksene diklestirilir");

    // ---- 3) DONUS NORMALIZASYONU ---------------------------------------------
    const MemberGeometry rot180 =
        memberGeometryFromProfile(top, bottom, true, Vec3{1.0, 0.0, 0.0}, 180.0, hea240);
    CHECK(std::fabs(rot180.profileRotation - 180.0) < 1e-12, "180° korunur");
    const MemberGeometry rotNeg =
        memberGeometryFromProfile(top, bottom, true, Vec3{1.0, 0.0, 0.0}, -90.0, hea240);
    CHECK(std::fabs(rotNeg.profileRotation - 270.0) < 1e-12, "-90° -> 270°");
    const MemberGeometry rot360 =
        memberGeometryFromProfile(top, bottom, true, Vec3{1.0, 0.0, 0.0}, 360.0, hea240);
    CHECK(std::fabs(rot360.profileRotation) < 1e-12, "360° -> 0°");

    // ---- 4) DUSEY UYEDE (n ∥ axis) DEJENERE OLMAMALI -------------------------
    // Kolon ekseni Z; n = Z verilirse dunya Z kurali devreye girer.
    const MemberGeometry parallel =
        memberGeometryFromProfile(top, bottom, true, Vec3{0.0, 0.0, 1.0}, 0.0, hea240);
    CHECK(memberGeometryUsable(parallel), "n ∥ axis: geometri hala gecerli");
    const double nlen = std::sqrt(parallel.refNormal.x * parallel.refNormal.x +
                                  parallel.refNormal.y * parallel.refNormal.y +
                                  parallel.refNormal.z * parallel.refNormal.z);
    CHECK(std::fabs(nlen - 1.0) < 1e-9, "n ∥ axis: refNormal birim");
    const double dot = parallel.refNormal.x * 0.0 + parallel.refNormal.y * 0.0 +
                       parallel.refNormal.z * 1.0;
    CHECK(std::fabs(dot) < 1e-9, "n ∥ axis: refNormal eksene dik");

    // ---- 5) SIFIR UZUNLUK FAIL-CLOSED ---------------------------------------
    const MemberGeometry degen =
        memberGeometryFromProfile(top, top, true, Vec3{1.0, 0.0, 0.0}, 0.0, hea240);
    CHECK(!memberGeometryUsable(degen), "sifir uzunluk kullanilamaz (fail-closed)");

    // ---- 6) PLAN DIGEST: IKI UC SIRASI AYNI DIGEST --------------------------
    // Gercek generator (end plate) uzerinde: uc sirasi degisince digest AYNI.
    {
        auto makeInput = [&](const MemberGeometry& col, const MemberGeometry& beam) {
            ComponentInput in;
            in.main = col;
            in.secondary.push_back(beam);
            in.params.push_back(CompParam{L"plateWidth", 300.0});
            in.params.push_back(CompParam{L"plateHeight", 220.0});
            in.params.push_back(CompParam{L"plateThickness", 15.0});
            in.params.push_back(CompParam{L"boltDiameter", 20.0});
            in.params.push_back(CompParam{L"boltCountX", 2});
            in.params.push_back(CompParam{L"boltCountY", 2});
            in.params.push_back(CompParam{L"boltSpacingX", 120.0});
            in.params.push_back(CompParam{L"boltSpacingY", 90.0});
            in.params.push_back(CompParam{L"edgeDistance", 50.0});
            return in;
        };
        const MemberGeometry beamFwd =
            memberGeometryFromProfile(Vec3{0.0, 0.0, 3000.0}, Vec3{2000.0, 0.0, 3000.0},
                                      /*connectionAtA=*/true, Vec3{0.0, 0.0, 1.0},
                                      0.0, MemberSectionSpec{200.0, 400.0, 8.6, 13.5, 0.0});
        const MemberGeometry beamRev =
            memberGeometryFromProfile(Vec3{2000.0, 0.0, 3000.0}, Vec3{0.0, 0.0, 3000.0},
                                      /*connectionAtA=*/false, Vec3{0.0, 0.0, 1.0},
                                      0.0, MemberSectionSpec{200.0, 400.0, 8.6, 13.5, 0.0});
        CHECK(sameGeometry(beamFwd, beamRev), "kiris: uc sirasi ayni geometri");

        const GenerationPlan p1 = generateEndPlate(makeInput(forward, beamFwd));
        const GenerationPlan p2 = generateEndPlate(makeInput(reverse, beamRev));
        CHECK(p1.ok == p2.ok, "iki yon: plan ok durumu ayni");
        if (p1.ok && p2.ok) {
            const std::string d1 = generationPlanDigest(p1);
            const std::string d2 = generationPlanDigest(p2);
            CHECK(d1 == d2, "iki yon: planDigest AYNI (gevsetme yok)");
            if (d1 != d2)
                std::printf("  d1=%s\n  d2=%s\n", d1.c_str(), d2.c_str());
            std::printf("end plate planDigest: %s (levha %zu, bulon grubu %zu)\n",
                        d1.c_str(), p1.plates.size(), p1.boltGroups.size());
        } else {
            std::printf("  p1.ok=%d error=%ls\n", p1.ok ? 1 : 0, p1.error.c_str());
            std::printf("  p2.ok=%d error=%ls\n", p2.ok ? 1 : 0, p2.error.c_str());
        }
    }

    if (failures) {
        std::printf("%d kontrol basarisiz\n", failures);
        return 1;
    }
    std::printf("KANONIK UYE GEOMETRISI: TUM KONTROLLER GECTI\n");
    return 0;
}
