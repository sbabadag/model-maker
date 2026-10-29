// P2D — BAGIMSIZ (elle acilmis) LEVHA DELIKLERI: envanter + olculendirme (OCCT'siz, Qt'siz).
//
// Kapsam (kabul):
//   * Sahipsiz (bulon grubuna ait olmayan) gercek CutKind::Hole delikleri envantere GIRER;
//     kimlik kararli ve gercek bulon grubu kimlikleriyle CAKISMAZ.
//   * Olcu kaynagi: GERCEK Hole CutModifier + gecerli PlateSpec cercevesi. Izdusum dairesi ya da
//     bulon grubu UYDURULMAZ.
//   * Tek delik / duzgun dizilim -> cap-adet + adim + net kenar; duzensiz (kademeli/karisik) ->
//     her delik icin levha kenarindan GERCEK merkez koordinati (HoleOrdinate), sahte adim YOK.
//   * Donuk levha (PlateSpec u/v/n) ayni degerleri verir.
//   * Cerceve/eksen/derinlik cozulemezse: UNRESOLVED + TASLAK (sahte READY yok, delik sessizce dusmez).

#include "fab_fixture.hpp"
#include "model_maker/drawing_builder.hpp"
#include "model_maker/drawing_layout.hpp"
#include "model_maker/fabrication.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace mm;
using namespace mm::drawing;
#include "legacy_drawing_fixture.hpp"

static int failures = 0;
static int checks = 0;
static void check(bool ok, const std::string& label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
static bool nearTo(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

static std::vector<const Dimension2D*> dimsOf(const DrawingSheet& s, DimensionRole role, const std::string& fk = {}) {
    std::vector<const Dimension2D*> out;
    for (const auto& d : s.dimensions)
        if (d.role == role && (fk.empty() || d.featureKey == fk)) out.push_back(&d);
    return out;
}
static bool hasItem(const DrawingSheet& s, const char* key, ChecklistState st) {
    for (const auto& c : s.fabrication.checklist)
        if (c.key == key && c.state == st) return true;
    return false;
}
static bool nearValue(const std::vector<const Dimension2D*>& v, double x, double tol = 1e-6) {
    for (const auto* d : v)
        if (nearTo(d->value, x, tol)) return true;
    return false;
}

static CutModifier manualHole(EntityId plate, Vec3 origin, Vec3 normal, double dia, double slotAlong = 0.0,
                              double slotAcross = 0.0, double depth = 0.0) {
    CutModifier m;
    m.kind = CutKind::Hole;
    m.target = plate;
    m.planeOrigin = origin;
    m.planeNormal = normal;
    m.diameter = dia;
    m.owner = kInvalidEntityId;   // elle acilmis: bulon grubu YOK
    m.slotAlong = slotAlong;
    m.slotAcross = slotAcross;
    m.depth = depth;
    return m;
}

// Basit tek levha: 300x200x10, u=X, v=Z, n=Y; merkez (2500,0,0). Donusum fixture'in xf'sinden gelir.
static fx::Fixture plateCase(const fx::Xf& xf = fx::Xf{}) {
    fx::Fixture f = fx::make(xf, 4000.0, false);
    fx::addFramedPlate(f, {2500, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 300, 200, 10, "manuel_levha");
    return f;
}
static EntityId plateId(const fx::Fixture& f) {
    for (const auto& r : f.doc.componentInstances())
        for (const auto& c : r.children)
            if (c.kind == ComponentObjectKind::Plate && c.roleKey == "manuel_levha") return c.entityId;
    return kInvalidEntityId;
}
// Levha-yerel (u,v) -> dunya noktasi (levha cercevesi).
static Vec3 atUV(const fx::Fixture& f, double u, double v, double onNormal = 0.0) {
    const Vec3 c = f.xf.pt({2500, 0, 0});
    const Vec3 U = f.xf.vec({1, 0, 0}), V = f.xf.vec({0, 0, 1}), N = f.xf.vec({0, 1, 0});
    return {c.x + U.x * u + V.x * v + N.x * onNormal, c.y + U.y * u + V.y * v + N.y * onNormal,
            c.z + U.z * u + V.z * v + N.z * onNormal};
}
static Vec3 plateNormal(const fx::Fixture& f) { return f.xf.vec({0, 1, 0}); }

static fab::FabricationInventory inv(const fx::Fixture& f, EntityId plate) {
    return fab::extractFabrication(f.doc, {f.beam, plate}, f.beam);
}

int main() {
    // =========================================================================
    // A) TEK elle acilmis delik: envantere girer, cap/adet + adim + net kenar
    // =========================================================================
    {
        fx::Fixture f = plateCase();
        const EntityId p = plateId(f);
        f.doc.addCutModifier(manualHole(p, atUV(f, -100, 50), plateNormal(f), 14));
        const fab::FabricationInventory i = inv(f, p);
        check(!i.hasCode("HOLE_OWNER_UNKNOWN") && !i.hasCode("HOLE_FRAME_UNAVAILABLE"),
              "A: tek manual delik -> sahiplik/cerceve hatasi YOK (envantere girdi)");
        check(i.groups.size() == 1 && i.holes.size() == 1 && nearTo(i.groups[0].diameter, 14.0),
              "A: 1 grup / 1 delik, cap 14 (gercek CutModifier'dan)");
        check(i.groups[0].boltGroup != kInvalidEntityId && i.groups[0].boltGroup != f.groupFin &&
                  i.groups[0].boltGroup != f.groupUpper,
              "A: bagimsiz grup kimligi gercek bulon gruplariyla CAKISMIYOR, 0 da degil");
        check(i.holes[0].modifierIndex < f.doc.cutModifiers().size() &&
                  f.doc.cutModifiers()[i.holes[0].modifierIndex].kind == CutKind::Hole,
              "A: delik gercek Hole modifier'a bagli (izdusum/uydurma degil)");

        MarkRegistry reg;
        const BuildResult b = legacyBuildSheet(f.doc, reg, p, {});
        check(b.ok, "A: cizim uretildi");
        const DrawingSheet& s = b.sheet;
        const auto callouts = [&]() {
            int n = 0;
            for (const TextAnnotation& t : s.texts)
                if (t.kind == TextKind::HoleCallout && t.featureKey == i.groups[0].id) ++n;
            return n;
        };
        check(callouts() == 1, "A: cap/adet callout'u uretildi (1 x Ø14)");
        const auto edge = dimsOf(s, DimensionRole::HoleEdge, i.groups[0].id);
        check(edge.size() == 4 && nearValue(edge, 43.0) && nearValue(edge, 243.0) && nearValue(edge, 143.0),
              "A: 4 net kenar olcusu gercek degerlerle (43 / 243 / 43 / 143)");
        check(!s.fabrication.draft && hasItem(s, "HOLE_POSITIONS", ChecklistState::Satisfied) &&
                  hasItem(s, "HOLE_DIAMETER_COUNT", ChecklistState::Satisfied),
              "A: tek delik tam olculendi -> TASLAK degil, HOLE_POSITIONS satisfied");
        bool measured = true;
        for (const Dimension2D& d : s.dimensions) measured = measured && nearTo(d.measured(), d.value, 1e-6);
        check(measured, "A: her olcude measured() == value");
    }

    // =========================================================================
    // B) DUZENSIZ manual dizilim: sahte adim YOK, her delik gercek merkez koordinati
    // =========================================================================
    {
        fx::Fixture f = plateCase();
        const EntityId p = plateId(f);
        // Kademeli bes delik (dikdortgen ag DEGIL).
        const std::pair<double, double> uv[5] = {{-120, 60}, {-40, 60}, {30, 20}, {-90, -60}, {10, -70}};
        for (const auto& [u, v] : uv) f.doc.addCutModifier(manualHole(p, atUV(f, u, v), plateNormal(f), 12));
        const fab::FabricationInventory i = inv(f, p);
        check(i.holes.size() == 5 && i.groups.size() == 1, "B: bes manual delik tek bagimsiz grupta");
        check(i.hasCode("HOLE_PATTERN_IRREGULAR") && !i.groups[0].regular,
              "B: dizilim duzensiz olarak TESPIT edildi (sessizce duzgun sayilmadi)");

        MarkRegistry reg;
        const BuildResult b = legacyBuildSheet(f.doc, reg, p, {});
        check(b.ok, "B: cizim uretildi");
        const DrawingSheet& s = b.sheet;
        const std::string fk = i.groups[0].id;
        const auto pitch = dimsOf(s, DimensionRole::HolePitch, fk);
        const auto ord = dimsOf(s, DimensionRole::HoleOrdinate, fk);
        check(pitch.empty(), "B: duzensiz dizilimde periyodik adim olcusu URETILMEDI (sahte adim yok)");
        check(ord.size() == 10, "B: her delik icin iki gercek merkez koordinati (5 x 2 = 10)");
        // Gercek u/v: envMinU = -150, envMinV = -100.
        bool coords = true;
        for (const auto& [u, v] : uv) coords = coords && nearValue(ord, u + 150.0) && nearValue(ord, v + 100.0);
        check(coords, "B: ordinate degerleri GERCEK u/v (levha min-u/min-v kenarindan)");
        bool measured = true;
        for (const auto* d : ord) measured = measured && nearTo(d->measured(), d->value, 1e-6);
        check(measured, "B: ordinate measured() == value (kaynak geometriyle tutarli)");
        check(!s.fabrication.draft && hasItem(s, "HOLE_POSITIONS", ChecklistState::Satisfied),
              "B: duzensiz dizilim yine de tam olculendi -> TASLAK degil");
    }

    // =========================================================================
    // C) KARISIK: bulon sahipli + manual delikler ayni levhada (mevcut davranis korunur)
    // =========================================================================
    {
        fx::Fixture f = fx::make({}, 4000.0, false);
        const std::size_t before = inv(f, f.finPlate).holes.size();
        f.doc.addCutModifier(manualHole(f.finPlate, f.xf.pt({1450, 80, 0}), {0, 1, 0}, 14));
        const fab::FabricationInventory i = inv(f, f.finPlate);
        check(before == 6 && i.holes.size() == 7, "C: bulon sahipli 6 delik AYNEN + 1 manual = 7");
        int manual = 0, bolt = 0;
        for (const fab::HoleGroupFeature& g : i.groups) (g.boltGroup == f.groupFin ? bolt : manual) += static_cast<int>(g.count());
        check(bolt == 6 && manual == 1, "C: bulon sahipli grup davranisi degismedi; manual delik ayri grupta");
        check(!i.hasCode("HOLE_OWNER_UNKNOWN"), "C: manual delik icin sahiplik hatasi URETILMIYOR");
    }

    // =========================================================================
    // D) DONUK levha: ayni degerler (PlateSpec u/v/n); montajda levha yuzu DETAY gorunumu
    // =========================================================================
    {
        fx::Fixture f = fx::make(fx::rotZ(30), 4000.0, true);   // assembly: levha yuzu detay gorunumu
        // Montaja ait DONUK kanat levhasina elle delikler (levha-yerel u,v).
        const Vec3 c = f.xf.pt({1500, 80, 0});
        const Vec3 U = f.xf.vec({1, 0, 0}), V = f.xf.vec({0, 0, 1});
        const auto holeOnFin = [&](double u, double v) {
            CutModifier m;
            m.kind = CutKind::Hole;
            m.target = f.finPlate;
            m.owner = kInvalidEntityId;
            m.diameter = 16;
            m.planeOrigin = {c.x + U.x * u + V.x * v, c.y + U.y * u + V.y * v, c.z + U.z * u + V.z * v};
            m.planeNormal = plateNormal(f);
            f.doc.addCutModifier(m);
        };
        holeOnFin(-110, 70);
        holeOnFin(20, -40);
        holeOnFin(60, 90);
        const fab::FabricationInventory i =
            fab::extractFabrication(f.doc, {f.beam, f.endPlate, f.finPlate}, f.beam);
        check(i.holes.size() == 3 + 16, "D: donuk montajda 3 manual delik envanterde (16 bulon + 3)");
        check(!i.hasCode("HOLE_NOT_NORMAL_TO_PLATE") && !i.hasCode("HOLE_OUTSIDE_PLATE"),
              "D: delik ekseni donuk levha normali ile uyumlu");
        std::string fk;
        for (const fab::HoleGroupFeature& g : i.groups)
            if (g.plate == f.finPlate && g.diameter > 15.0 && g.boltGroup != f.groupFin) fk = g.id;
        check(!fk.empty(), "D: donuk levhanin bagimsiz delik grubu bulundu");
        MarkRegistry reg;
        const BuildResult b = legacyBuildSheet(f.doc, reg, f.doc.assemblies()[0].id, {});
        check(b.ok, "D: donuk levha cizimi uretildi");
        const auto ord = dimsOf(b.sheet, DimensionRole::HoleOrdinate, fk);
        check(ord.size() == 6 && nearValue(ord, 40.0) && nearValue(ord, 170.0) && nearValue(ord, 210.0) &&
                  nearValue(ord, 60.0) && nearValue(ord, 190.0),
              "D: donuk levhada da gercek u/v degerleri (u+150 / v+100)");
    }

    // =========================================================================
    // E) Iki farkli levhada manual delikler: kararli, cakismayan, tekrar uretilebilir kimlik
    // =========================================================================
    {
        fx::Fixture f = fx::make({}, 4000.0, false);
        const EntityId a = fx::addFramedPlate(f, {2500, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 300, 200, 10, "m_a");
        const EntityId b = fx::addFramedPlate(f, {3000, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 200, 200, 10, "m_b");
        f.doc.addCutModifier(manualHole(a, {2400, 0, 0}, {0, 1, 0}, 12));
        f.doc.addCutModifier(manualHole(b, {2950, 0, 0}, {0, 1, 0}, 12));
        const fab::FabricationInventory i1 = fab::extractFabrication(f.doc, {f.beam, a, b}, f.beam);
        const fab::FabricationInventory i2 = fab::extractFabrication(f.doc, {f.beam, a, b}, f.beam);
        check(i1.groups.size() == 2, "E: iki levhada iki bagimsiz grup");
        std::set<EntityId> ids;
        for (const auto& g : i1.groups) ids.insert(g.boltGroup);
        check(ids.size() == 2 && !ids.count(kInvalidEntityId), "E: grup kimlikleri tekil ve gecerli");
        for (const BoltGroup& bg : f.doc.boltGroups()) ids.erase(bg.id);
        check(ids.size() == 2, "E: kimlikler gercek bulon gruplariyla cakismiyor");
        std::vector<std::string> k1, k2;
        for (const auto& g : i1.groups) k1.push_back(g.id);
        for (const auto& g : i2.groups) k2.push_back(g.id);
        check(k1 == k2, "E: yeniden uretimde AYNI featureKey'ler (kararli)");
    }

    // =========================================================================
    // F) Cerceve yok (PlateSpec yok): delik sessizce DUSMEZ -> UNRESOLVED + TASLAK
    // =========================================================================
    {
        fx::Fixture f = fx::make({}, 4000.0, false);
        const EntityId bare = fx::addFramedPlate(f, {2500, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 200, 100, 8,
                                                 "cercevesiz", false);
        f.doc.addCutModifier(manualHole(bare, {2450, 0, 20}, {0, 1, 0}, 20));
        const fab::FabricationInventory i = inv(f, bare);
        // 2026-10-28 DAVRANIS DEGISIKLIGI: kalici PlateSpec yoksa cerceve GERCEK
        // GEOMETRIDEN turetilir (dikdortgen levha) — kaydedilmis modelde child
        // payload'lari MMW8'e yazilmadigi icin delik olculeri tamamen
        // kayboluyordu. Olcu uydurulmaz: geometriden olculur.
        check(!i.hasCode("HOLE_FRAME_UNAVAILABLE") && i.holes.size() == 1,
              "F: PlateSpec yok -> cerceve geometriden turetildi, delik envanterde");
        MarkRegistry reg;
        const BuildResult b = legacyBuildSheet(f.doc, reg, bare, {});
        check(b.ok && !hasItem(b.sheet, "HOLES_UNRESOLVED", ChecklistState::Unresolved),
              "F: delik olcusu uretildi -> HOLES_UNRESOLVED yok (sahte READY yok)");
        const bool dims = !dimsOf(b.sheet, DimensionRole::HolePitch).empty() ||
                          !dimsOf(b.sheet, DimensionRole::HoleEdge).empty() ||
                          !dimsOf(b.sheet, DimensionRole::HoleOrdinate).empty();
        check(dims, "F: cercevesiz levhada delik konum olcusu VAR (tek delik -> kenar/ordinat)");
    }

    // =========================================================================
    // G) Kismi/egik manual delik: kaynak geometri yetersiz -> UNRESOLVED
    // =========================================================================
    {
        fx::Fixture f = plateCase();
        const EntityId p = plateId(f);
        f.doc.addCutModifier(manualHole(p, atUV(f, 0, 0), {0.3, 1, 0}, 14));
        f.doc.addCutModifier(manualHole(p, atUV(f, 40, 0), plateNormal(f), 14, 0, 0, 3.0));
        const fab::FabricationInventory i = inv(f, p);
        check(i.hasCode("HOLE_NOT_NORMAL_TO_PLATE") && i.hasCode("HOLE_PARTIAL_DEPTH"),
              "G: egik eksen + kismi derinlik acikca raporlandi");
        MarkRegistry reg;
        const BuildResult b = legacyBuildSheet(f.doc, reg, p, {});
        check(b.ok && b.sheet.fabrication.draft, "G: cozulemeyen delik -> TASLAK");
    }

    // =========================================================================
    // H) Sessiz delik kaybi yok: gecerli Hole modifier sayisi == envanterdeki delik sayisi
    // =========================================================================
    {
        fx::Fixture f = plateCase();
        const EntityId p = plateId(f);
        for (int k = 0; k < 7; ++k)
            f.doc.addCutModifier(manualHole(p, atUV(f, -120.0 + 40.0 * k, (k % 2) ? 40 : -40), plateNormal(f), 12));
        const fab::FabricationInventory i = inv(f, p);
        std::size_t valid = 0;
        for (const CutModifier& m : f.doc.cutModifiers())
            if (m.enabled && m.kind == CutKind::Hole && m.target == p && m.diameter > 0.0) ++valid;
        check(valid == 7 && i.holes.size() == valid, "H: 7 delik modifier -> 7 delik envanterde (kayip yok)");
    }

    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
