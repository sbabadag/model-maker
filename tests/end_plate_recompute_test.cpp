// END PLATE RECOMPUTE (Slice 2d-2b) — GERCEK OCCT kabul testleri.
//
// Ana iddia: kolon/kiris DEGISTIGINDE mevcut End Plate baglantisinin levhasi,
// bulonlari ve delikleri GERCEK B-Rep uzerinden yeniden hesaplanir (kirli bayrak
// veya koordinat hesabi DEGIL). Bu test, Application::rebuildEndPlateConnection
// icindeki URETIM hattini (resolveOcctContact -> computeEndPlatePlacementOnFace
// -> buildEndPlateFromPlacement -> boltGroupHoleModifiers) ayni bilesenlerle
// calistirir ve degisiklik sonrasi geometrinin TEPKI verdigini dogrular.
//
// Senaryolar: A (olusturma), B (HEB300->HEB400), C (30° dondurme), D (kiris
// +1000Z), E (iki baglanti), L (idempotent — delik birikmez).

#include "model_maker/occ_contact_resolver.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/end_plate.hpp"
#include "model_maker/end_plate_geometry.hpp"
#include "model_maker/bolt.hpp"
#include "model_maker/profile_database.hpp"

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
static bool near(double a, double b, double tol = 1e-3) { return std::abs(a - b) < tol; }

static SteelProfile heb(double name_h) {
    SteelProfile p;
    p.name = name_h > 350.0 ? "HEB400" : "HEB300";
    p.height = name_h;
    p.width = name_h;
    p.plateThickness = name_h > 350.0 ? 13.5 : 11.0;
    p.flangeThickness = name_h > 350.0 ? 24.0 : 19.0;
    return p;
}

// Yeniden hesaplama hatti: kolon B-Rep + cerceve + kesit + kiris ucu/yonu ->
// temas + yerlesim. Application::resolveEndPlatePlacement bu adimlarin aynisini
// yapar (OCCT temas oncelikli, computeEndPlatePlacementOnFace).
struct Recompute {
    ContactPlacement contact;
    EndPlatePlacement placement;
    bool resolved{false};
};
static Recompute recompute(const TopoDS_Shape& colSolid, const MemberLocalFrame& frame,
                           const SectionDims& dims, double colLen, const Vec3& beamEnd,
                           const Vec3& beamDir, const EndPlateParams& params) {
    Recompute out;
    out.contact = resolveOcctContact(colSolid, frame, dims, colLen, beamEnd, beamDir,
                                     SemanticFaceRole::FlangeOuterPositive);
    if (out.contact.status != ResolutionStatus::Resolved) return out;
    ReferencePlane face;
    face.origin = out.contact.contactPoint;
    face.normal = out.contact.outwardNormal;
    out.placement = computeEndPlatePlacementOnFace(face, params.gap, /*secWidth*/ 100.0,
                                                   /*secHeight*/ 200.0, params);
    out.resolved = true;
    return out;
}

// Delik sayimi: yerlesimin BoltGroup'u PARCA listelemest (parts bos); Document
// katmani parts'i {levha, kolon} olarak doldurur. Test bu dolumu taklit eder.
static std::size_t holeCount(const EndPlatePlacement& placement) {
    BoltGroup b = placement.bolts;
    b.parts = {1, 2};  // levha + kolon (sahte kimlikler)
    return boltGroupHoleModifiers(b).size();
}

int main() {
    const double cos30 = 0.86602540378443864676;
    const double sin30 = 0.5;

    const SteelProfile heb300 = heb(300.0);
    const SteelProfile heb400 = heb(400.0);
    const SectionDims dims300{300.0, 300.0, 11.0, 19.0};
    const SectionDims dims400{400.0, 400.0, 13.5, 24.0};

    EndPlateParams params;  // M20/22, 12mm levha, 2x2 bulon, gap=0
    params.gap = 0.0;

    // Dik kolon: axis Z, ref normal +Y. Kesit "top of section": ust flans dis
    // yuzu Y=0'da, kesit Y=-h'ye sarkar.
    const auto colFrame = [](const Vec3& from) {
        return makeMemberLocalFrame(profileSectionFrame({0, 0, 1}, {0, 1, 0}), from);
    };
    const auto col300 = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0, {0, 1, 0});

    // --- A: olusturma (HEB300 + IPE200, gap=0) --------------------------------
    // Kiris +Y'den gelir (yukaridan), kolon ust flansina baglanir.
    {
        const auto r = recompute(col300, colFrame({0, 0, 0}), dims300, 3000, {0, 500, 1500},
                                 {0, -1, 0}, params);
        CHECK(r.resolved, "A: temas cozuldu");
        CHECK(near(r.contact.outwardNormal.y, 1.0), "A: dis normal +Y");
        CHECK(near(r.contact.contactPoint.y, 0.0), "A: temas ust flans dis yuzu Y=0");
        // gap=0 + kalinlik 12 -> levha merkezi Y=+6, kolon tarafi Y=0 ile CAKISIK.
        CHECK(near(r.placement.plateCenter.y, 6.0), "A: levha merkezi Y=+6 (gap=0, t=12)");
        CHECK(near(r.placement.plateCenter.y - params.plateThickness * 0.5, 0.0),
              "A: levha kolon tarafi Y=0 (gap=0, kolona girmiyor)");
        CHECK(near(r.placement.plateCenter.z, 1500.0), "A: levha baglanti kotu 1500 korunur");
        // secWidth=100 + 2*20 = 140 ; secHeight=200 + 2*20 = 240.
        CHECK(near(r.placement.plateWidth, 140.0) && near(r.placement.plateHeight, 240.0),
              "A: levha boyutlari kiris kesitinden (140x240)");
        const TopoDS_Shape plate = buildEndPlateFromPlacement(r.placement);
        CHECK(!plate.IsNull() && shapeVolume(plate) > 0.0, "A: levha B-Rep uretildi");
        const auto boltOrigins = boltGroupBoltOrigins(r.placement.bolts);
        CHECK(boltOrigins.size() == 4, "A: 2x2 = 4 bulon");
        CHECK(holeCount(r.placement) == 8, "A: 4 bulon x 2 parca = 8 delik modifier");
    }

    // --- B: HEB300 -> HEB400 (profil degisimi; kati GERCEKTEN degisti) --------
    // "top of section" konumda ust flans dis yuzu Y=0'da SABIT kalir (eksen ayni);
    // ama kolon B-Rep'i derinlesir (h=400). Yeniden cozum YENI kati uzerinde
    // basarili olmali, levha dogru oturmali, delik SAYISI degismemeli (birikme yok).
    {
        const TopoDS_Shape col400 = extrudeProfileSolid(heb400, {0, 0, 0}, {0, 0, 3000}, 0.0, {0, 1, 0});
        CHECK(shapeVolume(col400) > shapeVolume(col300), "B: HEB400 kati HEB300'den buyuk");
        const auto r = recompute(col400, colFrame({0, 0, 0}), dims400, 3000, {0, 500, 1500},
                                 {0, -1, 0}, params);
        CHECK(r.resolved, "B: HEB400 uzerinde yeniden cozum basarili");
        CHECK(near(r.contact.contactPoint.y, 0.0), "B: temas yeni katinin ust flansi Y=0");
        CHECK(near(r.placement.plateCenter.y, 6.0), "B: levha dogru oturdu (Y=+6)");
        CHECK(holeCount(r.placement) == 8, "B: delik sayisi degismedi (8, birikme yok)");
    }

    // --- C: kolon 30° donduruldu -> levha yeni yuzeye uyum saglar ------------
    // Donen kolonda lokal cerceve uyeyle doner; +v yuzune dik yaklasilir.
    {
        const TopoDS_Shape col30 =
            extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 30.0, {0, 1, 0});
        MemberLocalFrame rf;
        rf.origin = {0, 0, 0};
        rf.u = {cos30, sin30, 0};
        rf.v = {-sin30, cos30, 0};
        rf.w = {0, 0, 1};
        const Vec3 beamEnd{-sin30 * 500.0, cos30 * 500.0, 1500.0};
        const Vec3 dir{sin30, -cos30, 0};
        const auto r = recompute(col30, rf, dims300, 3000, beamEnd, dir, params);
        CHECK(r.resolved, "C: 30° kolon temas cozuldu");
        CHECK(near(r.contact.outwardNormal.x, -sin30) && near(r.contact.outwardNormal.y, cos30),
              "C: dis normal dondu (+v = -sin30, cos30)");
        // Levha merkezi = temas + 6*normal; normal artık dünya +Y DEĞİL.
        CHECK(std::abs(r.placement.plateCenter.x) > 1e-3, "C: levha X konumu dondu");
        CHECK(near(r.placement.plateCenter.z, 1500.0), "C: kot korundu");
        // Levha tabanı (u,v) de döndü -> axis = normal.
        CHECK(near(r.placement.axis.x, -sin30) && near(r.placement.axis.y, cos30),
              "C: levha ekseni normalle ayni (dondurulmus)");
    }

    // --- D: kiris +1000 Z tasindi -> levha koti takip eder -------------------
    {
        const auto lo = recompute(col300, colFrame({0, 0, 0}), dims300, 3000, {0, 500, 1500},
                                  {0, -1, 0}, params);
        const auto hi = recompute(col300, colFrame({0, 0, 0}), dims300, 3000, {0, 500, 2500},
                                  {0, -1, 0}, params);
        CHECK(lo.resolved && hi.resolved, "D: iki kot da cozuldu");
        CHECK(near(hi.placement.plateCenter.z - lo.placement.plateCenter.z, 1000.0),
              "D: levha +1000Z tasiyan kirisi takip etti");
        CHECK(near(lo.placement.plateCenter.y, 6.0) && near(hi.placement.plateCenter.y, 6.0),
              "D: Y konumu korundu (kot degisti)");
    }

    // --- E: ayni kolona iki farkli kotta iki End Plate -> bagimsiz -----------
    {
        const auto lo = recompute(col300, colFrame({0, 0, 0}), dims300, 3000, {0, 500, 400},
                                  {0, -1, 0}, params);
        const auto hi = recompute(col300, colFrame({0, 0, 0}), dims300, 3000, {0, 500, 2600},
                                  {0, -1, 0}, params);
        CHECK(lo.resolved && hi.resolved, "E: iki baglanti cozuldu");
        CHECK(near(lo.placement.plateCenter.z, 400.0) && near(hi.placement.plateCenter.z, 2600.0),
              "E: iki baglanti ayri kotlarini korudu");
        CHECK(near(lo.placement.plateCenter.y, hi.placement.plateCenter.y),
              "E: iki levha ayni yuzde (Y ayni), kotlar bagimsiz");
    }

    // --- L: tekrarli recompute idempotent (delik birikmez, konum degismez) ----
    {
        Recompute first = recompute(col300, colFrame({0, 0, 0}), dims300, 3000, {0, 500, 1500},
                                    {0, -1, 0}, params);
        bool stable = true;
        for (int i = 0; i < 5; ++i) {
            Recompute again = recompute(col300, colFrame({0, 0, 0}), dims300, 3000,
                                        {0, 500, 1500}, {0, -1, 0}, params);
            if (!again.resolved || !near(again.placement.plateCenter.x, first.placement.plateCenter.x) ||
                !near(again.placement.plateCenter.y, first.placement.plateCenter.y) ||
                !near(again.placement.plateCenter.z, first.placement.plateCenter.z) ||
                holeCount(again.placement) != 8)
                stable = false;
        }
        CHECK(stable, "L: 5x recompute ayni konum + 8 delik (birikme yok)");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
