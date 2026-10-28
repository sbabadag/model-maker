// P2A — SEMANTIK IMALAT OZELLIKLERI testi (OCCT'siz, Qt'siz).
// Gercek Document -> fab::extractFabrication: delik cap/merkez/adim/net kenar, uye baslangic ofsetleri,
// donusum (dondurme/ayna) degismezligi, cozumlenemeyen durum RAPORU (uydurma YOK), mark ayrimi.

#include "fab_fixture.hpp"
#include "model_maker/drawing_builder.hpp"
#include "model_maker/fabrication.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace mm;
using namespace mm::fab;

static int failures = 0;
static int checks = 0;
static void check(bool ok, const char* label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool nearTo(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }
static bool vecNear(const std::vector<double>& a, std::initializer_list<double> b, double tol = 1e-6) {
    if (a.size() != b.size()) return false;
    std::size_t i = 0;
    for (const double v : b)
        if (!nearTo(a[i++], v, tol)) return false;
    return true;
}

static const HoleGroupFeature* groupByBolt(const FabricationInventory& inv, EntityId bg) {
    for (const auto& g : inv.groups)
        if (g.boltGroup == bg) return &g;
    return nullptr;
}

struct Summary {
    std::vector<double> numbers;   // transform-degismez ozet
};

static std::vector<double> invariantSummary(const fx::Fixture& f, const FabricationInventory& inv) {
    std::vector<double> s;
    for (const EntityId bg : {f.groupUpper, f.groupLower, f.groupFin}) {
        const HoleGroupFeature* g = groupByBolt(inv, bg);
        if (!g) { s.push_back(-1); continue; }
        s.push_back(g->diameter);
        s.push_back(g->count());
        for (const double v : g->colU) s.push_back(v);
        for (const double v : g->rowV) s.push_back(v);
        for (const double v : g->pitchU) s.push_back(v);
        for (const double v : g->pitchV) s.push_back(v);
        s.push_back(g->edgeMinU); s.push_back(g->edgeMaxU); s.push_back(g->edgeTop); s.push_back(g->edgeBottom);
    }
    for (const EntityId pid : {f.endPlate, f.finPlate}) {
        const PlateFeature* p = inv.plateOf(pid);
        if (!p) { s.push_back(-1); continue; }
        s.push_back(p->width); s.push_back(p->height); s.push_back(p->thickness);
        for (int k = 0; k < 3; ++k) s.push_back(std::fabs(p->offsetRef(k)));   // |ofset|: ayna isaret cevirir, buyukluk ayni
    }
    return s;
}

template <class F>
static void mutatePlateSpec(Document& doc, EntityId plate, F fn) {
    for (std::size_t i = 0; i < doc.componentInstances().size(); ++i) {
        ComponentInstanceRecord rec = doc.componentInstances()[i];
        bool hit = false;
        for (auto& ch : rec.children)
            if (ch.entityId == plate && ch.plate) { fn(*ch.plate); hit = true; }
        if (hit) doc.setComponentInstance(i, rec);
    }
}

static FabricationInventory extractAll(const fx::Fixture& f) {
    return extractFabrication(f.doc, {f.beam, f.endPlate, f.finPlate}, f.beam);
}

int main() {
    // =========================================================================
    // A) Gercek zemin gerceklik: donusumsuz fixture
    // =========================================================================
    const fx::Fixture f = fx::make();
    const FabricationInventory inv = extractAll(f);
    check(inv.datum.valid && inv.datum.member == f.beam, "A: MemberDatum gercek eksenden (kiris)");
    check(nearTo(inv.datum.length, 4000) && nearTo(inv.datum.start.x, 0) && nearTo(inv.datum.end.x, 4000),
          "A: datum baslangic (0,0,0) bitis (4000,0,0) uzunluk 4000");
    check(nearTo(inv.datum.x.x, 1) && nearTo(inv.datum.y.y, 1) && nearTo(inv.datum.z.z, 1),
          "A: yerel eksenler x'=X y'=Y z'=Z (konvansiyon)");
    check(inv.datum.rollResolved && !inv.datum.orientationBasis.empty(), "A: roll cozuldu + yonelim dayanagi yazili");
    check(inv.plates.size() == 2, "A: iki cerceveli levha");
    check(inv.holes.size() == 4 + 6 + 6, "A: toplam 16 gercek delik (4 + 6 + 6)");
    check(inv.groups.size() == 3, "A: 3 bagimsiz delik grubu (ust, alt, kanat)");

    const PlateFeature* E = inv.plateOf(f.endPlate);
    const PlateFeature* F = inv.plateOf(f.finPlate);
    check(E && nearTo(E->width, 200) && nearTo(E->height, 400) && nearTo(E->thickness, 20), "A: uc levhasi 200 x 400 x 20");
    check(F && nearTo(F->width, 300) && nearTo(F->height, 200) && nearTo(F->thickness, 10), "A: kanat plaka 300 x 200 x 10");
    check(E && E->material == "S355", "A: malzeme S355 PlateSpec'ten");
    check(E && E->outline.size() == 4 && E->outlineExact && !E->tapered, "A: duz dikdortgen kontur 4 kose");

    const HoleGroupFeature* U = groupByBolt(inv, f.groupUpper);
    const HoleGroupFeature* L = groupByBolt(inv, f.groupLower);
    const HoleGroupFeature* W = groupByBolt(inv, f.groupFin);
    check(U && U->regular && U->rows == 2 && U->cols == 2 && nearTo(U->diameter, 22) && U->count() == 4, "A: ust grup 2x2 Ø22");
    check(U && vecNear(U->colU, {-50, 50}) && vecNear(U->rowV, {140, 100}), "A: ust grup merkezleri u=-50,50 ; v=140,100 (ust sira ilk)");
    check(U && vecNear(U->pitchU, {100}) && vecNear(U->pitchV, {40}), "A: ust grup adimlari 100 / 40");
    check(U && nearTo(U->edgeMinU, 39) && nearTo(U->edgeMaxU, 39) && nearTo(U->edgeTop, 49),
          "A: ust grup NET kenarlar: sol/sag 39 (100-50-11), ust 49 (200-140-11)");
    check(L && L->regular && L->rows == 3 && L->cols == 2 && nearTo(L->diameter, 18) && L->count() == 6, "A: alt grup 2x3 Ø18");
    check(L && vecNear(L->rowV, {-50, -95, -140}) && vecNear(L->pitchV, {45, 45}) && vecNear(L->pitchU, {100}),
          "A: alt grup v=-50,-95,-140 ; adimlar 45,45 / 100");
    check(L && nearTo(L->edgeBottom, 51) && nearTo(L->edgeMinU, 41) && nearTo(L->edgeMaxU, 41),
          "A: alt grup NET alt kenar 51 (-140-9+200), yan 41");
    check(W && W->regular && W->cols == 3 && W->rows == 2 && nearTo(W->diameter, 22), "A: kanat grubu 3x2 Ø22");
    check(W && vecNear(W->colU, {-100, 0, 100}) && vecNear(W->rowV, {30, -30}) && vecNear(W->pitchU, {100, 100}) &&
              vecNear(W->pitchV, {60}),
          "A: kanat grubu merkezleri + adimlari 100,100 / 60");
    check(W && nearTo(W->edgeMinU, 39) && nearTo(W->edgeTop, 59) && nearTo(W->edgeBottom, 59), "A: kanat NET kenarlar 39 / 59 / 59");
    // Tek tek delik (row/col/u/v) ve kaynak modifier
    {
        const HoleFeature* h = nullptr;
        for (const auto& hh : inv.holes)
            if (hh.boltGroup == f.groupUpper && hh.row == 1 && hh.col == 2) h = &hh;
        check(h && nearTo(h->u, 50) && nearTo(h->v, 140) && nearTo(h->diameter, 22) && h->plate == f.endPlate &&
                  h->shape == HoleShape::Round,
              "A: delik (satir1,sutun2): u=50 v=140 Ø22 -- sahibi uc levhasi");
        check(h && f.doc.cutModifiers()[h->modifierIndex].kind == CutKind::Hole &&
                  f.doc.cutModifiers()[h->modifierIndex].owner == f.groupUpper,
              "A: delik gercek CutModifier(Hole) + BoltGroup sahibine bagli");
        check(h && h->id.find("hole:") == 0, "A: kararli delik anahtari 'hole:...'");
    }
    // Uye-yerel ofsetler (BASLANGICTAN)
    check(E && nearTo(E->localMin.x, 0) && nearTo(E->localMax.x, 20) && nearTo(E->offsetRef(0), 0) && nearTo(E->offsetRef(1), 0) &&
              nearTo(E->offsetRef(2), 0),
          "A: uc levhasi uye baslangicina DEGER: ofset 0/0/0");
    check(F && nearTo(F->localMin.x, 1350) && nearTo(F->localMax.x, 1650) && nearTo(F->offsetRef(0), 1350),
          "A: kanat plaka x' ofseti = 1350 (yakin yuz, baslangictan)");
    check(F && nearTo(F->localMin.y, 75) && nearTo(F->localMax.y, 85) && nearTo(F->offsetRef(1), 75),
          "A: kanat plaka y' ofseti = 75 (kiris eksenine yakin yuz)");
    {
        bool st = false;
        check(F && nearTo(F->offsetRef(2, &st), 0) && F->localMin.z < 0 && F->localMax.z > 0,
              "A: kanat plaka z' ekseni kesiyor -> merkez ofseti 0");
        (void)st;
    }
    check(inv.hasCode("WELD_NOT_DIMENSIONED") && !inv.hasCode("HOLE_FRAME_UNAVAILABLE"),
          "A: kaynak var -> WELD_NOT_DIMENSIONED raporlandi; delikler tam cozumlendi");
    check(inv.documentRevision == f.doc.revision(), "A: envanter Document surumunu tasir");

    // =========================================================================
    // B) Donusum degismezligi: dondurme (Z 30 + Y 15 + oteleme) ve ayna
    // =========================================================================
    {
        const std::vector<double> base = invariantSummary(f, inv);
        fx::Xf rot = fx::mul(fx::translate({1234, -777, 55}), fx::mul(fx::rotZ(30), fx::rotY(15)));
        const fx::Fixture fr = fx::make(rot);
        const FabricationInventory ir = extractAll(fr);
        const std::vector<double> sr = invariantSummary(fr, ir);
        bool same = base.size() == sr.size();
        double maxd = 0;
        for (std::size_t i = 0; same && i < base.size(); ++i) maxd = std::max(maxd, std::fabs(base[i] - sr[i]));
        check(same && maxd < 1e-6, "B: dondurulmus+oteli fixture: tum delik/levha/ofset degerleri AYNI (<1e-6 mm)");
        check(ir.datum.valid && nearTo(ir.datum.length, 4000), "B: dondurulmus uye ekseni uzunlugu 4000 korunur");
        check(ir.holes.size() == 16 && !ir.hasCode("HOLE_FRAME_UNAVAILABLE") && !ir.hasCode("PLATE_FRAME_STALE"),
              "B: dondurulmus: 16 delik, cerceve bayat degil");

        const fx::Fixture fm = fx::make(fx::mirrorX());
        const FabricationInventory im = extractAll(fm);
        const std::vector<double> sm = invariantSummary(fm, im);
        bool sameM = base.size() == sm.size();
        double maxm = 0;
        for (std::size_t i = 0; sameM && i < base.size(); ++i) maxm = std::max(maxm, std::fabs(base[i] - sm[i]));
        if (!(sameM && maxm < 1e-6))
            for (std::size_t i = 0; i < std::min(base.size(), sm.size()); ++i)
                if (std::fabs(base[i] - sm[i]) > 1e-6) std::printf("   idx %zu base %.4f mirror %.4f\n", i, base[i], sm[i]);
        check(sameM && maxm < 1e-6, "B: aynalanmis fixture (x->-x): delik/levha degerleri ve |ofset| AYNI");
        check(im.datum.valid && nearTo(im.datum.x.x, -1) && nearTo(im.datum.start.x, 0) && nearTo(im.datum.end.x, -4000),
              "B: aynali uye: eksen -X yonunde, baslangic (0,0,0) -- ofset BASLANGICTAN");
    }

    // =========================================================================
    // C) Uydurma YOK: cozumlenemeyen durumlar RAPORLANIR
    // =========================================================================
    {
        // C1: cercevesiz levhaya delik -> HOLE_FRAME_UNAVAILABLE ; levha ozelligi yok
        fx::Fixture g = fx::make({}, 4000, false);
        const EntityId bare = fx::addFramedPlate(g, {2500, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 200, 100, 8, "cercevesiz", false);
        fx::addBolts(g, bare, {2450, 80, 20}, {0, 1, 0}, {1, 0, 0}, 2, 1, 100, 0, 20);
        const FabricationInventory i = extractFabrication(g.doc, {g.beam, g.endPlate, g.finPlate, bare}, g.beam);
        // 2026-10-28 DAVRANIS DEGISIKLIGI: kalici PlateSpec yoksa cerceve artik
        // GERCEK GEOMETRIDEN turetilir (dikdortgen levha) — kaydedilip yeniden
        // acilan modelde child payload'lari MMW8'e yazilmadigi icin delik
        // konum olculeri tamamen kayboluyordu ("paper drawing delik yerlerine
        // olcu vermedi"). Olcu UYDURULMAZ: gercek geometriden olculur.
        check(!i.hasCode("HOLE_FRAME_UNAVAILABLE"), "C1: geometriden turetilen cerceve -> HOLE_FRAME_UNAVAILABLE YOK");
        const PlateFeature* bareF = i.plateOf(bare);
        check(bareF != nullptr, "C1: cercevesiz DIKDORTGEN levha geometriden turetildi (PlateFeature VAR)");
        check(i.holes.size() == 16 + 2, "C1: cercevesiz levhanin delikleri de envantere GIRER (16 + 2)");
        if (bareF) {
            const double w = bareF->width, h = bareF->height;
            const bool dimsOk = (std::fabs(w - 200.0) < 0.6 && std::fabs(h - 100.0) < 0.6) ||
                                (std::fabs(w - 100.0) < 0.6 && std::fabs(h - 200.0) < 0.6);
            check(dimsOk && std::fabs(bareF->thickness - 8.0) < 0.6,
                  "C1: turetilen cerceve gercek geometriyle ayni (200x100x8)");
        }
    }
    {
        // C2: sahipsiz delik
        fx::Fixture g = fx::make({}, 4000, false);
        CutModifier m = makeHoleCutModifier(g.finPlate, g.xf.pt({1450, 80, 0}), {0, 1, 0}, 14);
        g.doc.addCutModifier(m);   // owner = 0
        const FabricationInventory i = extractAll(g);
        check(!i.hasCode("HOLE_OWNER_UNKNOWN") && i.holes.size() == 17,
              "C2: sahipsiz (manuel) delik envantere GIRER: 16 bulon + 1 manual = 17 (P2D)");
        const HoleGroupFeature* man = nullptr;
        for (const HoleGroupFeature& grp : i.groups)
            if (grp.plate == g.finPlate && grp.boltGroup != g.groupFin) man = &grp;
        check(man != nullptr && man->count() == 1 && man->boltGroup != kInvalidEntityId,
              "C2: manual delik kararli, cakismayan bagimsiz grup kimligiyle envanterde");
    }
    {
        // C3: egik delik
        fx::Fixture g = fx::make({}, 4000, false);
        CutModifier m = makeHoleCutModifier(g.finPlate, g.xf.pt({1450, 80, 0}), {0.3, 1, 0}, 14);
        m.owner = g.groupFin;
        g.doc.addCutModifier(m);
        const FabricationInventory i = extractAll(g);
        check(i.hasCode("HOLE_NOT_NORMAL_TO_PLATE") && i.holes.size() == 16, "C3: levha normaline paralel olmayan delik -> HOLE_NOT_NORMAL_TO_PLATE");
    }
    {
        // C4: kismi derinlik
        fx::Fixture g = fx::make({}, 4000, false);
        CutModifier m = makeHoleCutModifier(g.finPlate, g.xf.pt({1450, 80, 0}), {0, 1, 0}, 14, 3.0);
        m.owner = g.groupFin;
        g.doc.addCutModifier(m);
        const FabricationInventory i = extractAll(g);
        check(i.hasCode("HOLE_PARTIAL_DEPTH"), "C4: levha kalinligini kapsamayan derinlik -> HOLE_PARTIAL_DEPTH");
        CutModifier thru = makeHoleCutModifier(g.finPlate, g.xf.pt({1450, 80, 0}), {0, 1, 0}, 14, 100.0);
        thru.owner = g.groupFin;
        fx::Fixture g2 = fx::make({}, 4000, false);
        g2.doc.addCutModifier(thru);
        const FabricationInventory i2 = extractAll(g2);
        check(!i2.hasCode("HOLE_PARTIAL_DEPTH") && i2.holes.size() == 17, "C4: levhayi kapsayan sinirli derinlik gecer (delik sayilir)");
    }
    {
        // C5: bayat cerceve (levha kati cerceveden tasiyor)
        fx::Fixture g = fx::make({}, 4000, false);
        mutatePlateSpec(g.doc, g.finPlate, [](PlateSpec& p) { p.center.x += 400; });   // kayit baska yerde
        const FabricationInventory i = extractAll(g);
        // 2026-10-28: bayat cerceve RAPORLANIR (TASLAK) ama olculendirme icin
        // geometriden turetilen cerceve kullanilir — aksi halde kullanici yine
        // hicbir delik olcusu gormuyordu.
        check(i.hasCode("PLATE_FRAME_STALE"), "C5: bayat PlateSpec cercevesi -> PLATE_FRAME_STALE raporlanir");
        check(i.plateOf(g.finPlate) != nullptr, "C5: olculendirme icin geometriden turetilen cerceve kullanilir");
    }
    {
        // C6: duzgun olmayan dizilim (silinen bulon) ve yuvali delik
        fx::Fixture g = fx::make({}, 4000, false);
        BoltGroup bg;
        bg.parts = {g.finPlate};
        bg.origin = {1400, 80, 30};
        bg.axis = {0, 1, 0};
        bg.xDir = {1, 0, 0};
        bg.countX = 3; bg.countY = 2; bg.spacingX = 100; bg.spacingY = 60;
        bg.deletedBolts = {2};
        bg.params.holeDiameter = 16;
        g.doc.addBoltGroup(bg, WireframeModel::line(bg.origin, bg.origin));
        const FabricationInventory i = extractAll(g);
        check(i.hasCode("HOLE_PATTERN_IRREGULAR"), "C6: silinmis bulonlu dizilim -> HOLE_PATTERN_IRREGULAR");
        BoltGroup sl = bg;
        sl.deletedBolts.clear();
        sl.origin = {1400, 80, -20};
        sl.shapeParts = {g.finPlate};
        sl.holeShapeTarget = 3;
        sl.shapeHoleDiameter = 18;
        sl.slotAlong = 14;
        sl.slotDir = {1, 0, 0};
        g.doc.addBoltGroup(sl, WireframeModel::line(sl.origin, sl.origin));
        const FabricationInventory i2 = extractAll(g);
        bool slotShape = false;
        for (const auto& h : i2.holes) slotShape = slotShape || h.shape == HoleShape::Slot;
        check(i2.hasCode("SLOT_NOT_DRAWN") && slotShape, "C6: yuvali delik sekli yakalanir, kontur cizilemedigi icin SLOT_NOT_DRAWN raporlanir");
    }
    {
        // C7: kesim ve kaynak
        fx::Fixture g = fx::make({}, 4000, true);
        CutModifier pc;
        pc.kind = CutKind::PlaneCut;
        pc.target = g.beam;
        pc.planeOrigin = {3900, 0, 0};
        pc.planeNormal = {1, 0, 0};
        g.doc.addCutModifier(pc);
        const FabricationInventory i = extractAll(g);
        check(i.hasCode("CUT_NOT_DIMENSIONED") && i.hasCode("WELD_NOT_DIMENSIONED"), "C7: kesim + kaynak -> CUT_/WELD_NOT_DIMENSIONED");
    }
    {
        // C8: pahli (konik) levha: kontur duz pah, yay pah -> yaklasik raporu
        fx::Fixture g = fx::make({}, 4000, false);
        mutatePlateSpec(g.doc, g.finPlate, [](PlateSpec& p) {
            p.chamfers.push_back(PlateSpec::Chamfer{1, 1, 100.0, 60.0});
            p.chamfers.push_back(PlateSpec::Chamfer{2, 1, 100.0, 60.0});
        });
        const FabricationInventory i = extractAll(g);
        const PlateFeature* P = i.plateOf(g.finPlate);
        check(P && P->tapered && P->outline.size() == 6 && P->outlineExact, "C8: iki duz pahli (konik) levha: 6 kose, tam kontur");
        if (P) {
            // CCW alan: pahsiz 300*200 = 60000 ; iki kose 0.5*100*60 = 3000 kesilir
            double a = 0;
            for (std::size_t k = 0; k < P->outline.size(); ++k) {
                const Vec2 p = P->outline[k], q = P->outline[(k + 1) % P->outline.size()];
                a += p.x * q.y - q.x * p.y;
            }
            check(nearTo(a / 2.0, 60000 - 2 * 3000, 1e-6), "C8: kontur alani 54000 (60000 - 2 x 3000) -- CCW");
        }
        mutatePlateSpec(g.doc, g.finPlate, [](PlateSpec& p) { p.chamfers[0].type = 2; });
        check(extractAll(g).hasCode("PLATE_OUTLINE_APPROX"), "C8: yay pah -> PLATE_OUTLINE_APPROX");
    }

    // =========================================================================
    // D) Mark ayrimi: ayni sekilli iki levha, FARKLI delikli
    // =========================================================================
    {
        fx::Fixture g = fx::make({}, 4000, false);
        const EntityId a = fx::addFramedPlate(g, {2000, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 200, 100, 10, "esdeger_a");
        const EntityId b = fx::addFramedPlate(g, {2600, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 200, 100, 10, "esdeger_a");
        const EntityId c = fx::addFramedPlate(g, {3200, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 200, 100, 10, "esdeger_a");
        const EntityId d = fx::addFramedPlate(g, {3800, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 200, 100, 10, "esdeger_a");
        fx::addBolts(g, a, {1950, 80, 0}, {0, 1, 0}, {1, 0, 0}, 2, 1, 100, 0, 22);   // a: 2 delik Ø22 (u=-50,+50)
        fx::addBolts(g, b, {2550, 80, 0}, {0, 1, 0}, {1, 0, 0}, 2, 1, 100, 0, 22);   // b: AYNI delik
        fx::addBolts(g, c, {3150, 80, 0}, {0, 1, 0}, {1, 0, 0}, 2, 1, 100, 0, 18);   // c: farkli cap
        fx::addBolts(g, d, {3820, 80, 0}, {0, 1, 0}, {1, 0, 0}, 2, 1, 60, 0, 22);    // d: farkli konum (u=+20,+80)
        MarkRegistry reg;
        mm::drawing::markDocument(g.doc, reg);
        const auto ma = reg.markOf(a), mb = reg.markOf(b), mc = reg.markOf(c), md = reg.markOf(d);
        check(ma && mb && mc && md, "D: dort levhaya da mark verildi");
        check(ma && mb && ma->number == mb->number, "D: ayni sekil + AYNI delik -> AYNI mark (gruplama korunur)");
        check(ma && mc && ma->number != mc->number, "D: ayni sekil, FARKLI delik capi -> FARKLI mark");
        check(ma && md && ma->number != md->number && mc->number != md->number, "D: ayni sekil, FARKLI delik konumu -> FARKLI mark");
        // Donusumden bagimsiz: aynalanmis/donmus ayni levha ayni mark
        fx::Xf rot = fx::mul(fx::translate({500, 300, 40}), fx::rotZ(37));
        fx::Fixture r = fx::make(rot, 4000, false);
        const EntityId ra = fx::addFramedPlate(r, {2000, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 200, 100, 10, "esdeger_a");
        fx::addBolts(r, ra, {1950, 80, 0}, {0, 1, 0}, {1, 0, 0}, 2, 1, 100, 0, 22);
        check(fab::drillingFingerprint(r.doc, ra) == fab::drillingFingerprint(g.doc, a),
              "D: dondurulmus/oteli ayni levhanin delik parmak izi AYNI (levha-yerel)");
        check(fab::drillingFingerprint(g.doc, g.beam) == 0, "D: deliksiz parca parmak izi 0 (imza degismez)");
    }

    std::printf(failures ? "FAILURES: %d / %d\n" : "ALL PASS (%d checks)\n", failures ? failures : checks, checks);
    return failures ? 1 : 0;
}
