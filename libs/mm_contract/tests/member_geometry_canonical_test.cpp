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

// Kesit MERKEZININ dunya koordinatindaki fiziksel noktasi: eksen ortasi +
// sectionCenterOffset * kesit yukseklik yonu (memberSectionFrame ile).
static Vec3 sectionCentroid(const MemberGeometry& g) {
    const ProfileSectionFrame f = memberSectionFrame(g);
    const Vec3 mid{(g.axisFrom.x + g.axisTo.x) * 0.5,
                   (g.axisFrom.y + g.axisTo.y) * 0.5,
                   (g.axisFrom.z + g.axisTo.z) * 0.5};
    return Vec3{mid.x + f.height.x * g.sectionCenterOffset,
                mid.y + f.height.y * g.sectionCenterOffset,
                mid.z + f.height.z * g.sectionCenterOffset};
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
    // n ve -n ayni KANONIK refNormal ve ayni cerceveyi verir (isaret kanoniklestirme).
    // AMA kesit merkez ofseti refNormal isaretine GORE isaretlidir: n -> -n cercevenin
    // yukseklik yonunu ters cevirir; fiziksel yerlesim korunacaksa ofset de negatiflenir.
    // (n ve -n ofset != 0 iken ayna goruntusu uyelerdir — ayni MemberGeometry DEGIL.)
    const MemberGeometry nPlus =
        memberGeometryFromProfile(top, bottom, true, Vec3{1.0, 0.0, 0.0}, 0.0, hea240);
    const MemberGeometry nMinus =
        memberGeometryFromProfile(top, bottom, true, Vec3{-1.0, 0.0, 0.0}, 0.0, hea240);
    CHECK(sameVec(nPlus.refNormal, nMinus.refNormal), "n/-n ayni kanonik refNormal");
    CHECK(nPlus.refNormal.x > 0.0, "refNormal isareti kanonik (ilk bilesen +)");
    CHECK(sameVec(memberSectionFrame(nPlus).width, memberSectionFrame(nMinus).width),
          "n/-n ayni kesit cercevesi (width)");
    CHECK(sameVec(memberSectionFrame(nPlus).height, memberSectionFrame(nMinus).height),
          "n/-n ayni kesit cercevesi (height)");
    CHECK(std::fabs(nPlus.sectionCenterOffset + nMinus.sectionCenterOffset) < 1e-12,
          "n/-n: ofset negatiflenir (fiziksel yerlesim korunur)");

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

    // ---- 4b) REGRESYON (42f61ee): n ∥ axis dusey uyede kesit cercevesi
    // profileSectionFrame'in ham fallback'iyle AYNI kalmalidir. d8e10b6'nin
    // 'dunya X' fallback'i dusey kolonun cercevesini 90° ceviriyordu
    // (Haunch 40 cap/alin levhasi kaymasi). Kanonik fonksiyon ciktilari,
    // memberSectionFrame ile ham profileSectionFrame(axis, rawN) esit olmali.
    {
        const Vec3 axis{0.0, 0.0, 1.0};  // dusey kolon ekseni
        const ProfileSectionFrame expect = profileSectionFrame(axis, Vec3{0.0, 0.0, 1.0});
        const ProfileSectionFrame got = memberSectionFrame(parallel);
        CHECK(sameVec(got.width, expect.width), "n∥axis: canonical width == raw width");
        CHECK(sameVec(got.height, expect.height), "n∥axis: canonical height == raw height");

        // 90° donuk kolonda da (regresyon senaryosu) cerceve AYNI sekilde doner.
        const MemberGeometry parallel90 =
            memberGeometryFromProfile(top, bottom, true, Vec3{0.0, 0.0, 1.0}, 90.0, hea240);
        const ProfileSectionFrame got90 = memberSectionFrame(parallel90);
        // rotateSectionFrame(f, 90°): w' = h, h' = -w (c=0, s=1).
        const ProfileSectionFrame expect90{
            expect.height,
            Vec3{-expect.width.x, -expect.width.y, -expect.width.z},
            expect.axis};
        CHECK(sameVec(got90.width, expect90.width), "n∥axis+90°: width ayni doner");
        CHECK(sameVec(got90.height, expect90.height), "n∥axis+90°: height ayni doner");
    }

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

    // ---- 7) ISARET-KANONIKLESTIRME + sectionCenterOffset (regresyon) ---------
    // Egik kiriste refNormal isaret-kanoniklestirmesi kesit cercevesinin genislik
    // VE yukseklik yonunu ters cevirir; sectionCenterOffset ayni anda negatiflenmezse
    // kesit bir tam yukseklik kadar yanlis tarafa kayar. Top/Bottom ankrajli
    // yatay + egik kiris, iki uc sirasiyla: fiziksel kesit merkezi HAM
    // profileSectionFrame(axis, rawN) + offset ile BIREBIR ayni kalmali.
    {
        const auto rawCentroid = [](const Vec3& from, const Vec3& to, const Vec3& rawN,
                                    double offset) {
            const ProfileSectionFrame f = profileSectionFrame(
                Vec3{to.x - from.x, to.y - from.y, to.z - from.z}, rawN);
            const Vec3 mid{(from.x + to.x) * 0.5, (from.y + to.y) * 0.5,
                           (from.z + to.z) * 0.5};
            return Vec3{mid.x + f.height.x * offset, mid.y + f.height.y * offset,
                        mid.z + f.height.z * offset};
        };
        const struct { Vec3 a, b; } cases[] = {
            {{0.0, 0.0, 0.0}, {6000.0, 0.0, 0.0}},     // yatay
            {{0.0, 0.0, 0.0}, {6000.0, 0.0, 1200.0}},  // egik (5.7°)
        };
        const MemberSectionSpec topSec{180.0, 400.0, 8.6, 13.5, -200.0};  // Top
        const MemberSectionSpec botSec{180.0, 400.0, 8.6, 13.5, 200.0};   // Bottom
        for (const auto& c : cases) {
            for (const MemberSectionSpec* spec : {&topSec, &botSec}) {
                const MemberGeometry fwd = memberGeometryFromProfile(
                    c.a, c.b, /*connectionAtA=*/true, Vec3{0.0, 0.0, 1.0}, 0.0, *spec);
                const MemberGeometry rev = memberGeometryFromProfile(
                    c.b, c.a, /*connectionAtA=*/false, Vec3{0.0, 0.0, 1.0}, 0.0, *spec);
                CHECK(sameGeometry(fwd, rev), "offset: uc sirasi ayni MemberGeometry");
                const Vec3 cRaw = rawCentroid(c.a, c.b, Vec3{0.0, 0.0, 1.0},
                                              spec->centerOffset);
                const Vec3 cFwd = sectionCentroid(fwd);
                CHECK(sameVec(cRaw, cFwd, 1e-6),
                      "offset: fiziksel kesit merkezi ham cerceve ile ayni (isaret-kanonik + offset)");
                if (!sameVec(cRaw, cFwd, 1e-6))
                    std::printf("  spec=%s raw=(%.3f,%.3f,%.3f) canon=(%.3f,%.3f,%.3f)\n",
                                spec == &topSec ? "Top" : "Bottom",
                                cRaw.x, cRaw.y, cRaw.z, cFwd.x, cFwd.y, cFwd.z);
            }
        }
    }

    // ---- 8) E KANITI: endplate_rotated_90 GOLDEN GIRDISI KANONIK DEGILDI -----
    // Eski golden, kirisi elde alan atamasiyla kuruyordu (axisFrom = baglanti ucu,
    // axisTo = uzak uc — kanonik kuralin tersi). Kanoniklestirme (memberGeometry
    // FromProfile) ayni FIZIKSEL uyeyi yeniden kurar; DIGEST degisir ama DUNYA
    // GEOMETRISI ayni kalir: levha kose noktalari, bulon merkezleri, kesim
    // duzlemi birebir (1e-6), yalniz cerceve isaretleri (u/v, xDir) ters.
    {
        const MemberSectionSpec beamSec{200.0, 400.0, 8.6, 13.5, 0.0};
        // ESKI (kanonik OLMAYAN): axisFrom = baglanti ucu {0,0,3000}.
        MemberGeometry oldBeam;
        oldBeam.id = 2;
        oldBeam.axisFrom = Vec3{0.0, 0.0, 3000.0};
        oldBeam.axisTo = Vec3{2000.0, 0.0, 3000.0};
        oldBeam.refNormal = Vec3{0.0, 0.0, 1.0};
        oldBeam.secWidth = 200.0; oldBeam.secHeight = 400.0;
        oldBeam.secWebThickness = 8.6; oldBeam.secFlangeThickness = 13.5;
        oldBeam.profileRotation = 90.0;
        // YENI (kanonik): memberGeometryFromProfile, baglanti ucu axisTo'da.
        MemberGeometry newBeam = memberGeometryFromProfile(
            Vec3{0.0, 0.0, 3000.0}, Vec3{2000.0, 0.0, 3000.0},
            /*connectionAtA=*/true, Vec3{0.0, 0.0, 1.0}, 90.0, beamSec);
        newBeam.id = 2;
        // Kok neden: ESKI girdi baglanti ucunu (kolon tepesi {0,0,3000}) axisFrom'a
        // koyuyor; kanonik kural axisTo = baglanti ucu der. isCanonicalMemberGeometry
        // bunu YAKALAYAMAZ (MemberGeometry baglanti-ucu bilgisini tasimaz; yalniz
        // refNormal/donus idempotence'ini olcer). Ekseni YON olarak karsilastir.
        const Vec3 connPoint{0.0, 0.0, 3000.0};  // kolon tepesi = baglanti ucu
        CHECK(oldBeam.axisFrom == connPoint && oldBeam.axisTo != connPoint,
              "eski kiris: baglanti ucu axisFrom'da (KONVANSIYONA AYKIRI)");
        CHECK(newBeam.axisTo == connPoint && newBeam.axisFrom != connPoint,
              "yeni kiris: baglanti ucu axisTo'da (kanonik)");

        const auto epInput = [](const MemberGeometry& beam) {
            ComponentInput in;
            const MemberSectionSpec colSec{300.0, 300.0, 11.0, 19.0, 0.0};
            in.main = memberGeometryFromProfile(Vec3{0.0, 0.0, 0.0},
                                                Vec3{0.0, 0.0, 3000.0},
                                                /*connectionAtA=*/false,
                                                Vec3{0.0, 0.0, 1.0}, 0.0, colSec);
            in.main.id = 1;
            in.secondary.push_back(beam);
            return in;
        };
        const GenerationPlan po = generateEndPlate(epInput(oldBeam));
        const GenerationPlan pn = generateEndPlate(epInput(newBeam));
        CHECK(po.ok && pn.ok, "E kaniti: iki plan da ok");
        if (po.ok && pn.ok) {
            // Levha DUNYA kose noktalari (outline yerel u/v + center/u/v).
            const auto worldCorners = [](const PlateSpec& p) {
                std::vector<Vec3> cs;
                for (const auto& o : p.outline) {
                    cs.push_back(Vec3{p.center.x + p.u.x * o.first + p.v.x * o.second,
                                      p.center.y + p.u.y * o.first + p.v.y * o.second,
                                      p.center.z + p.u.z * o.first + p.v.z * o.second});
                }
                return cs;
            };
            CHECK(po.plates.size() == 1 && pn.plates.size() == 1, "E kaniti: tek levha");
            if (po.plates.size() == 1 && pn.plates.size() == 1) {
                const auto co = worldCorners(po.plates[0]);
                const auto cn = worldCorners(pn.plates[0]);
                CHECK(co.size() == cn.size(), "E kaniti: kose sayisi ayni");
                bool allMatch = co.size() == cn.size();
                for (const auto& q : co) {
                    bool found = false;
                    for (const auto& r : cn)
                        if (sameVec(q, r, 1e-6)) { found = true; break; }
                    if (!found) allMatch = false;
                }
                CHECK(allMatch, "E kaniti: levha dunya kose noktalari AYNI (1e-6)");
                if (!allMatch) {
                    std::printf("  eski kose:\n");
                    for (const auto& q : co) std::printf("    (%.4f,%.4f,%.4f)\n", q.x, q.y, q.z);
                    std::printf("  yeni kose:\n");
                    for (const auto& q : cn) std::printf("    (%.4f,%.4f,%.4f)\n", q.x, q.y, q.z);
                }
            }
            // Bulon merkezleri (dunya) kume olarak AYNI.
            CHECK(po.boltGroups.size() == pn.boltGroups.size(), "E kaniti: bulon grubu sayisi");
            if (po.boltGroups.size() == pn.boltGroups.size() &&
                !po.boltGroups.empty() && !pn.boltGroups.empty()) {
                const auto bo = boltGroupBoltOrigins(po.boltGroups[0]);
                const auto bn = boltGroupBoltOrigins(pn.boltGroups[0]);
                bool boltMatch = bo.size() == bn.size();
                for (const auto& q : bo) {
                    bool found = false;
                    for (const auto& r : bn)
                        if (sameVec(q, r, 1e-6)) { found = true; break; }
                    if (!found) boltMatch = false;
                }
                CHECK(boltMatch, "E kaniti: bulon merkezleri AYNI (1e-6)");
            }
            // Kesim duzlemi AYNI (planeOrigin + planeNormal; keepPositive ile isaret).
            CHECK(po.cuts.size() == pn.cuts.size(), "E kaniti: kesim sayisi");
            if (po.cuts.size() == pn.cuts.size() && !po.cuts.empty() && !pn.cuts.empty()) {
                const auto& co2 = po.cuts[0]; const auto& cn2 = pn.cuts[0];
                const bool samePos = po.cuts[0].keepPositive == pn.cuts[0].keepPositive;
                const Vec3 no = co2.planeNormal, nn = cn2.planeNormal;
                const bool normSame = samePos ? sameVec(no, nn, 1e-6)
                                              : sameVec(no, Vec3{-nn.x, -nn.y, -nn.z}, 1e-6);
                CHECK(sameVec(co2.planeOrigin, cn2.planeOrigin, 1e-6) && normSame,
                      "E kaniti: kesim duzlemi AYNI");
            }
            // DIGEST farkli (beklenen: kanoniklestirme cerceve isaretini degistirdi).
            const std::string do2 = generationPlanDigest(po);
            const std::string dn2 = generationPlanDigest(pn);
            CHECK(do2 != dn2, "E kaniti: digest degisti (cerceve isaret farki)");
            std::printf("  eski digest=%s\n  yeni digest=%s\n", do2.c_str(), dn2.c_str());
        }
    }

    if (failures) {
        std::printf("%d kontrol basarisiz\n", failures);
        return 1;
    }
    std::printf("KANONIK UYE GEOMETRISI: TUM KONTROLLER GECTI\n");
    return 0;
}
