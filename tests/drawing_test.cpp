// DRAWING FOUNDATION (P1) — veri modeli + model->cizim boru hatti testi (OCC'siz, Qt'siz).
//
// Uctan uca: Document -> mark -> Front/Top/Side 2B gorunus -> A3 yerlesim -> mark etiketi +
// olculer -> JSON. Olculer GERCEK model geometrisinden BAGIMSIZ hesapla karsilastirilir.

#include "model_maker/drawing_builder.hpp"
#include "model_maker/weld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

using namespace mm;
using namespace mm::drawing;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool nearTo(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

static WireframeModel makeBox(Vec3 lo, Vec3 hi, const char* profile = "IPE300", const char* name = "",
                              bool withAxis = true) {
    std::vector<Vec3> v;
    for (int i = 0; i < 8; ++i)
        v.push_back({(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z});
    std::vector<Edge> e;
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t b = 1; b < 8; b <<= 1)
            if (!(i & b)) e.push_back({i, i | b});
    std::vector<Face> f{{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
    WireframeModel m(std::move(v), std::move(e), std::move(f));
    EntityProperties p;
    p.profileName = profile;
    p.name = name;
    if (withAxis) {
        p.axisFromX = lo.x; p.axisFromY = (lo.y + hi.y) / 2; p.axisFromZ = (lo.z + hi.z) / 2;
        p.axisToX = hi.x;   p.axisToY = (lo.y + hi.y) / 2;   p.axisToZ = (lo.z + hi.z) / 2;
    }
    m.setProperties(p);
    return m;
}

static EntityId add(Document& doc, WireframeModel m) {
    doc.addModel(std::move(m));
    return doc.modelId(doc.models().size() - 1);
}

static Weld weldOf(EntityId a, EntityId b) {
    Weld w;
    w.mainPart = a;
    w.secondaryPart = b;
    w.weldClass = WeldClass::Shop;
    return w;
}

// Modelin ortho projeksiyon sinirlari (bagimsiz referans): eksen secimi elle.
struct Ext { double w, h; };
static Ext extentOf(const WireframeModel& m, ViewKind k) {
    double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
    for (const Vec3& p : m.vertices()) {
        double a = 0, b = 0;
        switch (k) {
        case ViewKind::Front: a = p.x; b = p.z; break;   // X yatay, Z dikey
        case ViewKind::Top: a = p.x; b = p.y; break;     // X yatay, Y dikey
        case ViewKind::Side: a = p.y; b = p.z; break;    // Y, Z (yatay isaretten bagimsiz)
        }
        lo[0] = std::min(lo[0], a); hi[0] = std::max(hi[0], a);
        lo[1] = std::min(lo[1], b); hi[1] = std::max(hi[1], b);
    }
    return {hi[0] - lo[0], hi[1] - lo[1]};
}

static const Dimension2D* findDim(const DrawingSheet& s, ElementId view, DimensionRole role) {
    for (const auto& d : s.dimensions)
        if (d.viewId == view && d.role == role) return &d;
    return nullptr;
}
static int countMarks(const DrawingSheet& s, ElementId view) {
    int n = 0;
    for (const auto& m : s.marks) n += m.viewId == view;
    return n;
}
static bool overlaps(const Bounds2& a, const Bounds2& b, double gap) {
    return a.min.x < b.max.x + gap && b.min.x < a.max.x + gap &&
           a.min.y < b.max.y + gap && b.min.y < a.max.y + gap;
}

int main() {
    // =========================================================================
    // A) Tek parca: 6000 x 150 x 300 kiris.
    // =========================================================================
    // Projeksiyon sozlesmesi: Z yukari, sag-el (muhendislik konvansiyonu).
    {
        const Vec3 p{1, 2, 3};
        const Vec2 f = projectToView(p, ViewKind::Front), t = projectToView(p, ViewKind::Top),
                   s = projectToView(p, ViewKind::Side);
        check(nearTo(f.x, 1) && nearTo(f.y, 3) && nearTo(t.x, 1) && nearTo(t.y, 2) && nearTo(s.x, 2) && nearTo(s.y, 3),
              "P: Front=(x,z) Top=(x,y) Side=(y,z) -- Z her elevation'da YUKARI");
    }

    Document doc;
    const auto beamModel = makeBox({0, 0, 0}, {6000, 150, 300}, "IPE300", "Kiri\u015f");
    const EntityId beam = add(doc, beamModel);
    MarkRegistry reg;
    const BuildResult r = buildSheet(doc, reg, beam);
    check(r.ok && r.error.empty(), "A: buildSheet basarili");
    const DrawingSheet& sheet = r.sheet;
    check(sheet.kind == DrawingKind::SinglePart && sheet.subject == beam, "A: tek parca cizimi, konu = parca");
    check(sheet.name == "P1" && sheet.titleBlock.drawingNumber == "P1", "A: cizim adi/numarasi P1");
    check(sheet.titleBlock.title.find("Part P1") == 0 && sheet.titleBlock.title.find("Kiri\u015f") != std::string::npos,
          "A: title block basligi 'Part P1 - Kiris'");
    check(nearTo(sheet.format.width, 420) && nearTo(sheet.format.height, 297), "A: A3 yatay 420x297");
    check(sheet.views.size() == 3, "A: Front/Top/Side = 3 gorunus");

    const DrawingView* front = sheet.viewOfKind(ViewKind::Front);
    const DrawingView* top = sheet.viewOfKind(ViewKind::Top);
    const DrawingView* side = sheet.viewOfKind(ViewKind::Side);
    check(front && top && side, "A: uc gorunus da var");
    if (!front || !top || !side) return 1;

    // Gorunur kenar: dikdortgen = 4 benzersiz segment (arka/on kenarlar cakisir, uc-gorunus noktalari atilir).
    check(front->segments.size() == 4 && top->segments.size() == 4 && side->segments.size() == 4,
          "A: her gorunus 4 benzersiz gorunur segment");
    check(std::all_of(front->segments.begin(), front->segments.end(),
                      [](const Segment2& s) { return s.style == LineStyle::Visible; }),
          "A: varsayilan yalniz Visible");
    // Gorunus boyutlari GERCEK geometriden (bagimsiz hesap).
    for (const DrawingView* v : {front, top, side}) {
        const Ext e = extentOf(beamModel, v->kind);
        check(nearTo(v->bounds.width(), e.w) && nearTo(v->bounds.height(), e.h) &&
                  nearTo(v->bounds.min.x, 0) && nearTo(v->bounds.min.y, 0),
              "A: gorunus siniri = modelin gercek projeksiyon siniri, yerel orijinde");
    }
    check(nearTo(front->bounds.width(), 6000) && nearTo(front->bounds.height(), 300), "A: Front 6000x300");
    check(nearTo(top->bounds.width(), 6000) && nearTo(top->bounds.height(), 150), "A: Top 6000x150");
    check(nearTo(side->bounds.width(), 150) && nearTo(side->bounds.height(), 300), "A: Side 150x300");

    // Olcek: A3 cizim alani 1:20'de (6150/20 + 35) siger, 1:10'da sigmaz.
    check(sheet.scaleIsStandard && nearTo(sheet.scale, 1.0 / 20.0) && sheet.titleBlock.scaleText == "1:20",
          "A: standart olcek 1:20 secildi");
    for (const DrawingView& v : sheet.views) check(nearTo(v.scale, sheet.scale), "A: tum gorunusler ayni olcek");

    // Yerlesim: cizim alani icinde, birbirine binmez, 3. aci duzeni (Top ustte, Side sagda).
    {
        const Bounds2 area = sheet.drawingArea();
        bool inside = true;
        for (const DrawingView& v : sheet.views) {
            const Bounds2 pb = v.paperBounds();
            inside &= pb.min.x >= area.min.x && pb.max.x <= area.max.x && pb.min.y >= area.min.y &&
                      pb.max.y <= area.max.y;
        }
        check(inside, "A: tum gorunusler kagit cizim alani (title block disinda) icinde");
        check(!overlaps(front->paperBounds(), top->paperBounds(), 0) &&
                  !overlaps(front->paperBounds(), side->paperBounds(), 0) &&
                  !overlaps(top->paperBounds(), side->paperBounds(), 0),
              "A: gorunusler birbirine binmez");
        check(top->paperBounds().min.y > front->paperBounds().max.y &&
                  nearTo(top->paperBounds().centre().x, front->paperBounds().centre().x, 1e-6),
              "A: Top, Front'un USTUNDE ve ayni eksende (3. aci)");
        check(side->paperBounds().min.x > front->paperBounds().max.x &&
                  nearTo(side->paperBounds().centre().y, front->paperBounds().centre().y, 1e-6),
              "A: Side, Front'un SAGINDA ve ayni yukseklikte");
        // Title block alani (sag alt) ile cakisma yok.
        Bounds2 tb;
        tb.include({sheet.format.width - sheet.format.margin - sheet.format.titleWidth, sheet.format.margin});
        tb.include({sheet.format.width - sheet.format.margin, sheet.format.margin + sheet.format.titleHeight});
        bool clear = true;
        for (const DrawingView& v : sheet.views) clear &= !overlaps(v.paperBounds(), tb, 0);
        check(clear, "A: gorunusler title block ile cakismaz");
    }

    // Olculer.
    {
        const Dimension2D* len = findDim(sheet, front->id, DimensionRole::MemberLength);
        const Dimension2D* h = findDim(sheet, front->id, DimensionRole::OverallHeight);
        check(len && nearTo(len->value, 6000) && len->direction == DimensionDirection::Horizontal &&
                  nearTo(len->measured(), 6000) && len->source == beam,
              "A: Front uye boyu 6000 (yatay, eksenden, konu = parca)");
        check(findDim(sheet, front->id, DimensionRole::OverallWidth) == nullptr,
              "A: boyla ayni genel genislik tekrar edilmez");
        check(h && nearTo(h->value, 300) && nearTo(h->measured(), 300) && h->direction == DimensionDirection::Vertical,
              "A: Front genel yukseklik 300");
        const Dimension2D* tlen = findDim(sheet, top->id, DimensionRole::MemberLength);
        const Dimension2D* th = findDim(sheet, top->id, DimensionRole::OverallHeight);
        check(tlen && nearTo(tlen->value, 6000) && th && nearTo(th->value, 150), "A: Top boy 6000, derinlik 150");
        const Dimension2D* sw = findDim(sheet, side->id, DimensionRole::OverallWidth);
        const Dimension2D* sh = findDim(sheet, side->id, DimensionRole::OverallHeight);
        check(findDim(sheet, side->id, DimensionRole::MemberLength) == nullptr,
              "A: eksen gorunuse dik -> Side'da uye boyu olculmez");
        check(sw && nearTo(sw->value, 150) && sh && nearTo(sh->value, 300), "A: Side 150 x 300");
        // L) UZUN OLCU TEKRARI YOK (kullanici geri bildirimi 2026-10-28: "dis tan disa 5000 alakasiz"):
        // uye boyu olcusu olan gorunuste AYNI yonde genel (bounding box) olcu eklenmez — iki uzun
        // olcu ust uste binince hangisinin ne oldugu anlasilmiyor.
        {
            bool longDup = false;
            int memberLenDims = 0;
            for (const Dimension2D& d : sheet.dimensions) {
                if (d.role != DimensionRole::MemberLength) continue;
                ++memberLenDims;
                for (const Dimension2D& e : sheet.dimensions)
                    if (e.viewId == d.viewId && e.direction == d.direction &&
                        (e.role == DimensionRole::OverallWidth || e.role == DimensionRole::OverallHeight))
                        longDup = true;
            }
            check(memberLenDims >= 2 && !longDup,
                  "A: uye boyu varken ayni yonde ikinci uzun (bbox) olcu YOK (5000 / 4810 tekrari)");
        }
        bool tied = true;
        for (const Dimension2D& d : sheet.dimensions) {
            const DrawingView* v = sheet.findView(d.viewId);
            tied &= v != nullptr && nearTo(d.measured(), d.value, 1e-6) && d.source != kInvalidEntityId;
            // Olcu noktalari gorunus sinirina bagli (geometriden turetilmis, yerel).
            tied &= d.p1.x >= v->bounds.min.x - 1e-6 && d.p1.x <= v->bounds.max.x + 1e-6 &&
                    d.p2.y >= v->bounds.min.y - 1e-6 && d.p2.y <= v->bounds.max.y + 1e-6;
        }
        check(tied, "A: her olcunun yeniden olculen degeri = degeri; noktalari gorunus siniri icinde");
        // Gercek geometri ile capraz dogrulama (model degisirse olcu degisir).
        Document doc2;
        const EntityId beam2 = add(doc2, makeBox({0, 0, 0}, {7250, 150, 300}, "IPE300"));
        MarkRegistry reg2;
        const BuildResult r2 = buildSheet(doc2, reg2, beam2);
        const Dimension2D* len2 = r2.ok ? findDim(r2.sheet, r2.sheet.viewOfKind(ViewKind::Front)->id,
                                                   DimensionRole::MemberLength)
                                        : nullptr;
        check(len2 && nearTo(len2->value, 7250), "A: model boyu 7250 olunca olcu 7250 (geometriye bagli)");
    }

    // Mark etiketleri: her gorunumde P1; ankraj gorunus sinirinin icinde.
    {
        bool ok = true;
        for (const DrawingView& v : sheet.views) {
            ok &= countMarks(sheet, v.id) == 1;
            for (const auto& m : sheet.marks)
                if (m.viewId == v.id)
                    ok &= m.text == "P1/IPE300" && !m.isAssembly && m.target == beam &&
                          m.anchor.x >= v.bounds.min.x && m.anchor.x <= v.bounds.max.x &&
                          m.anchor.y >= v.bounds.min.y && m.anchor.y <= v.bounds.max.y;
        }
        check(ok, "A: her gorunumde tek 'P1' etiketi, ankraj gorunus icinde");
        check(reg.markOf(beam)->text() == L"P1", "A: kayit defterinde parca P1");
    }
    {
        std::set<ElementId> ids;
        for (const auto& v : sheet.views) ids.insert(v.id);
        for (const auto& m : sheet.marks) ids.insert(m.id);
        for (const auto& d : sheet.dimensions) ids.insert(d.id);
        check(ids.size() == sheet.views.size() + sheet.marks.size() + sheet.dimensions.size(),
              "A: cizim ici ElementId'ler tekil");
    }

    // =========================================================================
    // B) Kagit uzayi model uzayindan AYRI: modeli uzaga tasimak cizimi degistirmez.
    // =========================================================================
    {
        Document docMoved;
        const EntityId bm = add(docMoved, makeBox({100000, -50000, 20}, {106000, -49850, 320}, "IPE300", "Kiri\u015f"));
        MarkRegistry regM;
        const BuildResult rm = buildSheet(docMoved, regM, bm);
        bool same = rm.ok && rm.sheet.views.size() == sheet.views.size();
        for (std::size_t i = 0; same && i < sheet.views.size(); ++i) {
            const DrawingView& a = sheet.views[i];
            const DrawingView& b = rm.sheet.views[i];
            same &= a.kind == b.kind && nearTo(a.origin.x, b.origin.x, 1e-6) && nearTo(a.origin.y, b.origin.y, 1e-6) &&
                    nearTo(a.scale, b.scale) && a.segments.size() == b.segments.size();
            for (std::size_t k = 0; same && k < a.segments.size(); ++k)
                same &= nearTo(a.segments[k].a.x, b.segments[k].a.x, 1e-6) &&
                        nearTo(a.segments[k].b.y, b.segments[k].b.y, 1e-6);
        }
        for (std::size_t i = 0; same && i < sheet.dimensions.size() && i < rm.sheet.dimensions.size(); ++i)
            same &= nearTo(sheet.dimensions[i].value, rm.sheet.dimensions[i].value, 1e-6) &&
                    nearTo(sheet.dimensions[i].p1.x, rm.sheet.dimensions[i].p1.x, 1e-6);
        check(same, "B: modeli (1e5, -5e4) otelemek kagit uzayi sonucunu degistirmez");
        check(rm.ok && (rm.sheet.views[0].projectionOffset.x != sheet.views[0].projectionOffset.x ||
                        rm.sheet.views[0].projectionOffset.y != sheet.views[0].projectionOffset.y),
              "B: model-uzayi izi yalniz projectionOffset'te (gorunus verisi ayni)");
    }

    // =========================================================================
    // C) Assembly: kiris + 2 ayni levha (shop weld) -> A1 + part markalari.
    // =========================================================================
    {
        Document docA;
        const EntityId b = add(docA, makeBox({0, 0, 0}, {4000, 120, 240}, "IPE240", "Ana"));
        const EntityId pl1 = add(docA, makeBox({0, -10, 0}, {12, 130, 240}, "PL12", "Levha"));
        const EntityId pl2 = add(docA, makeBox({3988, -10, 0}, {4000, 130, 240}, "PL12", "Levha"));
        docA.addWeld(weldOf(b, pl1));
        docA.addWeld(weldOf(b, pl2));
        check(docA.assemblies().size() == 1, "C: weld tek assembly olusturdu");
        if (docA.assemblies().size() != 1) return 1;
        const EntityId asmId = docA.assemblies()[0].id;
        MarkRegistry regA;
        const BuildResult ra = buildSheet(docA, regA, asmId);
        check(ra.ok && ra.sheet.kind == DrawingKind::Assembly && ra.sheet.name == "A1", "C: assembly cizimi A1");
        if (!ra.ok) return 1;
        check(regA.markOf(b)->text() == L"P1" && regA.markOf(pl1)->text() == L"P2" &&
                  regA.markOf(pl2)->text() == L"P2",
              "C: kiris P1, iki ayni levha AYNI P2");
        const DrawingView* fv = ra.sheet.viewOfKind(ViewKind::Front);
        std::set<std::string> texts;
        int asmLabels = 0;
        for (const auto& m : ra.sheet.marks)
            if (m.viewId == fv->id) { texts.insert(m.text); asmLabels += m.isAssembly; }
        check(texts == std::set<std::string>({"A1/IPE240", "P1/IPE240", "P2/PL12"}) && countMarks(ra.sheet, fv->id) == 3 && asmLabels == 1,
              "C: Front'ta A1/IPE240 + P1/IPE240 + P2/PL12 (ayni levha tek etiket, profil ekli)");
        {
            // Etiket kutulari birbirine binmez ve sayfa cercevesinin icinde kalir.
            bool clear = true, inside = true;
            for (const DrawingView& v : ra.sheet.views) {
                std::vector<Bounds2> boxes;
                for (const auto& m : ra.sheet.marks)
                    if (m.viewId == v.id) boxes.push_back(markLabelBox(m));
                for (std::size_t i = 0; i < boxes.size(); ++i) {
                    inside &= boxes[i].min.x >= ra.sheet.format.margin && boxes[i].max.x <= ra.sheet.format.width - ra.sheet.format.margin &&
                              boxes[i].max.y <= ra.sheet.format.height - ra.sheet.format.margin;
                    for (std::size_t j = i + 1; j < boxes.size(); ++j) clear &= !overlaps(boxes[i], boxes[j], 0);
                }
            }
            check(clear, "C: ayni gorunumdeki etiket kutulari birbirine binmez");
            check(inside, "C: etiket kutulari sayfa cercevesi icinde");
        }
        const Dimension2D* len = findDim(ra.sheet, fv->id, DimensionRole::MemberLength);
        check(len && nearTo(len->value, 4000) && len->source == docA.assemblies()[0].mainPart,
              "C: assembly boyu ana parcanin ekseninden (4000)");
        const Dimension2D* oh = findDim(ra.sheet, fv->id, DimensionRole::OverallHeight);
        check(oh && nearTo(oh->value, 240), "C: assembly genel yukseklik 240");
        // Levhalar kiriş uçlarında: genel genişlik = bütün assembly (4000), ana parça boyuyla aynı → tekrar yok.
        check(nearTo(fv->bounds.width(), 4000), "C: assembly Front genisligi 4000 (tum uyeler)");

        // Yeni kayit defteriyle ayni dokuman: ayni mark (deterministik).
        MarkRegistry regB;
        const BuildResult ra1 = buildSheet(docA, regB, asmId);
        check(ra1.ok && ra1.sheet.name == "A1", "C: ayni dokuman yeniden -> yine A1 (deterministik)");
    }

    // =========================================================================
    // D) Hatalar.
    // =========================================================================
    {
        MarkRegistry r2;
        check(!buildSheet(doc, r2, EntityId{987654}).ok, "D: olmayan eleman reddedilir");
        Document d3;
        WireframeModel construction = makeBox({0, 0, 0}, {10, 10, 10});
        construction.setConstructionKind(ConstructionKind::Line);
        const EntityId c = add(d3, construction);
        MarkRegistry r3;
        const BuildResult bad = buildSheet(d3, r3, c);
        check(!bad.ok && !bad.error.empty(), "D: konstruksiyon nesnesi cizilmez, acik hata");
        const BuildResult none = buildSheet(doc, r3, kInvalidEntityId);
        check(!none.ok, "D: gecersiz EntityId reddedilir");
    }

    // =========================================================================
    // E) Hidden-line (deneysel): dondurulmus kutuda arka kenar Hidden olur.
    // =========================================================================
    {
        Document dh;
        auto rotated = makeBox({0, 0, 0}, {400, 300, 200}, "PL", "", false);
        rotated.rotateAroundZ({200, 150, 100}, 0.5235987755982988); // 30 derece
        const EntityId rh = add(dh, rotated);
        MarkRegistry rr;
        DrawingOptions off, on;
        on.hiddenLines = true;
        on.views = {ViewKind::Front};
        off.views = {ViewKind::Front};
        const BuildResult a = buildSheet(dh, rr, rh, off);
        const BuildResult b = buildSheet(dh, rr, rh, on);
        auto count = [](const DrawingView& v, LineStyle s) {
            return std::count_if(v.segments.begin(), v.segments.end(), [&](const Segment2& x) { return x.style == s; });
        };
        check(a.ok && b.ok && count(a.sheet.views[0], LineStyle::Hidden) == 0,
              "E: hiddenLines kapali -> Hidden segment yok");
        check(count(b.sheet.views[0], LineStyle::Hidden) >= 1 && count(b.sheet.views[0], LineStyle::Visible) >= 4,
              "E: hiddenLines acik -> en az 1 Hidden + Visible kenarlar");
        const auto viewLength = [](const DrawingView& v) {
            double sum = 0.0;
            for (const Segment2& seg : v.segments) sum += std::hypot(seg.b.x - seg.a.x, seg.b.y - seg.a.y);
            return sum;
        };
        // Gizli cizgi geometri UYDURMAZ: ayni cizgiye dusen arka kenarlar kirpildigi icin toplam
        // cizgi uzunlugu kapali hali ASAMAZ; ayrica 0.05 mm altinda kil parca uretilmez.
        bool noSlivers = true;
        for (const Segment2& seg : b.sheet.views[0].segments)
            noSlivers &= std::hypot(seg.b.x - seg.a.x, seg.b.y - seg.a.y) >= 0.05;
        check(noSlivers, "E: gizli cizgi kil (sliver) parca uretmez (>= 0.05 mm)");
        check(viewLength(b.sheet.views[0]) <= viewLength(a.sheet.views[0]) + 1.0,
              "E: gizli cizgi cakisan arka kenari ikinci kez cizmez (toplam uzunluk artmaz)");
    }

    // =========================================================================
    // F) Olcek / kagit / projeksiyon yontemi.
    // =========================================================================
    {
        Document dh;
        const EntityId huge = add(dh, makeBox({0, 0, 0}, {1000000, 1000, 2000}, "X"));
        MarkRegistry rr;
        const BuildResult big = buildSheet(dh, rr, huge);
        check(big.ok && !big.sheet.scaleIsStandard && big.sheet.scale < 1.0 / 1000.0 &&
                  big.sheet.titleBlock.scaleText.rfind("1:", 0) == 0,
              "F: 1000 m boyunda parca -> standart olmayan olcek, yine de sigar");
        bool fits = true;
        for (const auto& v : big.sheet.views) {
            const Bounds2 pb = v.paperBounds(), area = big.sheet.drawingArea();
            fits &= pb.min.x >= area.min.x - 1e-6 && pb.max.x <= area.max.x + 1e-6 && pb.min.y >= area.min.y - 1e-6 &&
                    pb.max.y <= area.max.y + 1e-6;
        }
        check(fits, "F: standart olmayan olcekte de cizim alani icinde");

        DrawingOptions first;
        first.projection = ProjectionMethod::FirstAngle;
        MarkRegistry r1;
        const BuildResult fa = buildSheet(doc, r1, beam, first);
        const DrawingView* f = fa.sheet.viewOfKind(ViewKind::Front);
        const DrawingView* t = fa.sheet.viewOfKind(ViewKind::Top);
        const DrawingView* s = fa.sheet.viewOfKind(ViewKind::Side);
        check(fa.ok && t->paperBounds().max.y < f->paperBounds().min.y &&
                  s->paperBounds().max.x < f->paperBounds().min.x,
              "F: 1. aci -> Top Front'un ALTINDA, Side SOLUNDA");

        DrawingOptions a4;
        a4.sheet = SheetSize::A4;
        a4.views = {ViewKind::Front};
        MarkRegistry r4;
        const BuildResult sm = buildSheet(doc, r4, beam, a4);
        check(sm.ok && nearTo(sm.sheet.format.width, 297) && sm.sheet.views.size() == 1 && sm.sheet.scale <= 1.0 / 20.0,
              "F: A4 + yalniz Front");
        check(!buildSheet(doc, r4, beam, [] { DrawingOptions o; o.views = {}; return o; }()).ok,
              "F: gorunum listesi bos -> acik hata");
    }

    // =========================================================================
    // G) Serilestirme: gidis-donus birebir, bozuk/surumsuz reddi, sonraki id devami.
    // =========================================================================
    {
        DrawingDocument dd;
        DrawingSheet s2 = sheet;
        s2.titleBlock.project = "Hal \u00c7at\u0131s\u0131";
        s2.titleBlock.revisions.push_back({"A", "2026-10-27", "\u0130lk yay\u0131n"});
        dd.addSheet(s2);
        dd.addSheet(sheet);
        const std::string json = dd.serialize();
        DrawingDocument back;
        std::string err;
        check(DrawingDocument::deserialize(json, back, err) && err.empty(), "G: deserialize basarili");
        check(back.serialize() == json, "G: gidis-donus birebir");
        check(back.sheets().size() == 2 && back.sheets()[0].titleBlock.project == "Hal \u00c7at\u0131s\u0131" &&
                  back.sheets()[0].titleBlock.revisions.size() == 1 &&
                  back.sheets()[0].views.size() == 3 && back.sheets()[0].dimensions.size() == sheet.dimensions.size() &&
                  back.sheets()[0].marks.size() == sheet.marks.size(),
              "G: sayfalar, title block (Turkce), revizyon, gorunus/olcu/mark sayilari korunur");
        check(nearTo(back.sheets()[0].views[0].segments[0].a.x, sheet.views[0].segments[0].a.x, 0) &&
                  nearTo(back.sheets()[0].dimensions[0].value, sheet.dimensions[0].value, 0),
              "G: koordinatlar/olculer bit-bit korunur (double tam yazim)");
        check(back.sheets()[0].id == 1 && back.sheets()[1].id == 2, "G: sheet kimlikleri atandi");
        DrawingSheet more;
        back.addSheet(more);
        check(back.sheets()[2].id == 3, "G: yuklemeden sonra sheet id devam eder");
        DrawingDocument untouched;
        untouched.addSheet(sheet);
        const std::string before = untouched.serialize();
        check(!DrawingDocument::deserialize("{ bozuk", untouched, err) && !err.empty() && untouched.serialize() == before,
              "G: bozuk JSON reddedilir, hedef degismez");
        check(!DrawingDocument::deserialize("{\"version\":42}", untouched, err), "G: bilinmeyen surum reddedilir");
        std::string tampered = json;
        const auto p = tampered.find("\"front\"");
        if (p != std::string::npos) tampered.replace(p, 7, "\"bogus\"");
        check(!DrawingDocument::deserialize(tampered, untouched, err) && untouched.serialize() == before,
              "G: bilinmeyen gorunus turu reddedilir");
        // Deterministik uretim.
        MarkRegistry rx;
        const BuildResult again = buildSheet(doc, rx, beam);
        DrawingDocument d1, d2;
        d1.addSheet(sheet);
        d2.addSheet(again.sheet);
        check(d1.serialize() == d2.serialize(), "G: ayni girdi -> bayt-bayt ayni cizim");
    }

    // =========================================================================
    // H) Deliklerin cizime ve olculere girmesi + montaj uye konum zinciri.
    // =========================================================================
    {
        Document dp;
        // Plaka X 0..300, Y 0..20 (kalinlik), Z 0..200; delik ekseni Y (Front'a DIK).
        const EntityId plate = add(dp, makeBox({0, 0, 0}, {300, 20, 200}, "PL20", "Plaka", false));
        const double holeX[2] = {50, 250}, holeZ[2] = {40, 160};
        for (double x : holeX)
            for (double z : holeZ) {
                CutModifier h;
                h.kind = CutKind::Hole;
                h.target = plate;
                h.planeOrigin = {x, 10, z};
                h.planeNormal = {0, 1, 0};
                h.diameter = 22;
                dp.addCutModifier(h);
            }
        MarkRegistry rp;
        DrawingOptions p1opt;
        p1opt.autoLayout = false;   // P1 dogrusal satir yerlesimi (bu bolum P1 zincir iliskisini dogrular; P2A motoru ayri testte)
        const BuildResult hp = buildSheet(dp, rp, plate, p1opt);
        check(hp.ok, "H: delikli plaka cizimi uretildi");
        const DrawingView* f = hp.sheet.viewOfKind(ViewKind::Front);
        const DrawingView* t = hp.sheet.viewOfKind(ViewKind::Top);
        check(f && f->circles.size() == 4, "H: Front'ta 4 delik dairesi (eksen gorunuse dik)");
        if (!f || !t) return 1;
        bool geomOk = true;
        for (const Circle2& c : f->circles) {
            bool match = false;
            for (double x : holeX)
                for (double z : holeZ) match |= nearTo(c.center.x, x) && nearTo(c.center.y, z);
            geomOk &= match && nearTo(c.radius, 11) && c.owner == plate;
        }
        check(geomOk, "H: daire merkezleri/yaricap modeldeki Hole kayitlariyla birebir (x,z)=(50|250,40|160), r=11");
        // 2026-10-28: levha cercevesi GEOMETRIDEN turetildigi icin delik etiketi
        // P1 daire etiketi degil P2A callout'udur (tek adet, "4 x Ø22").
        int callouts = 0;
        std::string calloutText;
        for (const TextAnnotation& t : hp.sheet.texts)
            if (t.kind == TextKind::HoleCallout && !t.lines.empty()) {
                ++callouts;
                calloutText = t.lines.front();
            }
        check(callouts == 1 && calloutText == std::string("4 x \xC3\x98") + "22",
              "H: tek delik callout'u '4 x Ø22'");
        check(t->circles.empty() && std::none_of(t->segments.begin(), t->segments.end(),
                                                 [](const Segment2& s) { return s.style == LineStyle::Hidden; }),
              "H: Top'ta (eksen gorunus duzleminde) hiddenLines kapaliyken delik cizilmez");

        // Zincir: kenar-delik-delik-kenar. Beklenen degerler MODEL kayitlarindan bagimsiz hesaplanir.
        // 2026-10-28: levha cercevesi geometriden turetildigi icin delik zinciri
        // P2A semantigindedir: NET KENAR (delik dis kenari -> levha kenari) +
        // merkez adimi. Eski P1 yedek zinciri merkez->kenar (50) veriyordu.
        std::vector<double> hDims, vDims;
        double hOff = -1, vOff = -1, ovOff = -1;
        for (const Dimension2D& d : hp.sheet.dimensions) {
            if (d.viewId != f->id) continue;
            const bool holeDim = d.role == DimensionRole::HolePitch || d.role == DimensionRole::HoleEdge;
            if (holeDim) {
                (d.direction == DimensionDirection::Horizontal ? hDims : vDims).push_back(d.value);
                if (d.direction == DimensionDirection::Horizontal) hOff = d.offset;
                else vOff = d.offset;
                check(nearTo(d.measured(), d.value, 1e-6) && d.source == plate,
                      "H: zincir olcusu yeniden olcumle esit, kaynak = plaka");
            }
            if (d.role == DimensionRole::OverallWidth) ovOff = d.offset;
        }
        std::sort(hDims.begin(), hDims.end());
        std::sort(vDims.begin(), vDims.end());
        // Ø22 -> r=11: net kenar = 50-11 = 39; adim = 200 (yatay), 120 (dusey).
        check(hDims.size() == 3 && nearTo(hDims[0], 39) && nearTo(hDims[1], 39) && nearTo(hDims[2], 200),
              "H: yatay zincir 39 (net kenar) / 200 (adim) / 39 (net kenar)");
        check(vDims.size() == 3 && nearTo(vDims[0], 29) && nearTo(vDims[1], 29) && nearTo(vDims[2], 120),
              "H: dusey zincir 29 / 120 / 29");
        check(hOff > 0 && ovOff > hOff, "H: delik zinciri genel olcunun ICINDE (daha yakin satir)");
        double sum = 0;
        for (double v : hDims) sum += v;
        check(nearTo(sum, 278), "H: zincir toplami = plaka genisligi - 2r (300 - 22)");

        // hiddenLines: Top'ta delik kenarlari kesik cizgi, tekrarsiz (2 farkli x * 2 kenar = 4).
        DrawingOptions hid;
        hid.hiddenLines = true;
        MarkRegistry rh;
        const BuildResult hh = buildSheet(dp, rh, plate, hid);
        const DrawingView* th = hh.sheet.viewOfKind(ViewKind::Top);
        const auto hiddenCount = std::count_if(th->segments.begin(), th->segments.end(),
                                               [](const Segment2& s) { return s.style == LineStyle::Hidden; });
        check(hiddenCount == 4, "H: hiddenLines -> Top'ta 4 tekrarsiz kesik delik cizgisi");

        // JSON gidis-donus: daire, yeni roller, textLevel.
        DrawingDocument dd;
        dd.addSheet(hp.sheet);
        const std::string js = dd.serialize();
        DrawingDocument back;
        std::string err;
        check(DrawingDocument::deserialize(js, back, err) && back.serialize() == js &&
                  back.sheets()[0].views[0].circles.size() + back.sheets()[0].views[1].circles.size() +
                          back.sheets()[0].views[2].circles.size() == 4,
              "H: daireler ve delik olculeri JSON gidis-donusunde korunur");
    }
    {
        // Assembly: kiris + iki uc levhasi -> uye konum zinciri.
        Document da;
        const EntityId b = add(da, makeBox({0, 0, 0}, {4000, 120, 240}, "IPE240", "Ana"));
        const EntityId p1 = add(da, makeBox({0, -10, 0}, {12, 130, 240}, "PL12", "Levha"));
        const EntityId p2 = add(da, makeBox({3988, -10, 0}, {4000, 130, 240}, "PL12", "Levha"));
        da.addWeld(weldOf(b, p1));
        da.addWeld(weldOf(b, p2));
        MarkRegistry ra;
        const BuildResult as = buildSheet(da, ra, da.assemblies()[0].id);
        const DrawingView* f = as.sheet.viewOfKind(ViewKind::Front);
        std::vector<double> pos;
        int shortWithLevel = 0, wideWithLevel = 0;
        for (const Dimension2D& d : as.sheet.dimensions)
            if (d.viewId == f->id && d.role == DimensionRole::PartPosition && d.direction == DimensionDirection::Horizontal) {
                pos.push_back(d.value);
                // P2A: dar olcunun metni merkezden kaydirilir (cizgi uzantisi) veya kademelenir; genis olan merkezde kalir.
                const bool moved = d.textLevel > 0 || std::fabs(d.textT - 0.5) > 1e-9;
                if (d.value * as.sheet.scale < 5.0) shortWithLevel += moved;
                else wideWithLevel += moved;
            }
        std::sort(pos.begin(), pos.end());
        check(pos.size() == 3 && nearTo(pos[0], 12) && nearTo(pos[1], 12) && nearTo(pos[2], 3976),
              "H: assembly Front uye konum zinciri 12 / 3976 / 12 (plaka-kiris-plaka)");
        check(shortWithLevel == 2 && wideWithLevel == 0,
              "H: kagitta cok dar (12 mm @ olcek) olculerin metni kademelendirilir, genis olan degil");
        double sum = 0;
        for (double v : pos) sum += v;
        check(nearTo(sum, f->bounds.width()), "H: konum zinciri toplami = gorunus genisligi");
    }

    // =========================================================================
    // E) EKSEN > GOVDE: eksen uclari gorunen govde ile cakismiyorsa eksen boyu YAZILMAZ
    //    (kullanici geri bildirimi: "5000 olcusu hangi uzunluga ait oldugu belli degil")
    // =========================================================================
    {
        Document dx;
        WireframeModel m = makeBox({0, 0, 0}, {4810, 150, 300}, "IPE300", "Kiris", false);
        EntityProperties pp = m.properties();
        pp.axisFromX = -95.0;                       // eksen 5000, gorunen govde 4810
        pp.axisToX = 4905.0;
        pp.axisFromY = pp.axisToY = 75.0;
        pp.axisFromZ = pp.axisToZ = 150.0;
        m.setProperties(pp);
        const EntityId idAxis = add(dx, m);
        MarkRegistry regAxis;
        const BuildResult rb = buildSheet(dx, regAxis, idAxis);
        const DrawingView* fv = rb.sheet.viewOfKind(ViewKind::Front);
        check(rb.ok && fv != nullptr, "E: eksen/govde uyusmazliginda cizim uretildi");
        if (fv) {
            check(findDim(rb.sheet, fv->id, DimensionRole::MemberLength) == nullptr,
                  "E: eksen uclari govde ile cakismiyor -> eksen boyu olcusu YAZILMAZ (havada olcu yok)");
            const Dimension2D* ow = findDim(rb.sheet, fv->id, DimensionRole::OverallWidth);
            check(ow && nearTo(ow->value, 4810) && nearTo(ow->measured(), 4810),
                  "E: onun yerine CIZILEN govde uzunlugu (4810) olculur");
        }
    }

    // =========================================================================
    // J) GIZLI CIZGI — GERCEK OKKLUZYON: onunde duran levha, arkadaki kiris kenarini
    //    KISMI olarak gizler (eski arka-yuz testi bunu goremiyordu).
    // =========================================================================
    {
        Document docO;
        const EntityId beamO = add(docO, makeBox({0, 0, 0}, {4000, 120, 240}, "IPE240", "Kiris"));
        // Front gorunumu -Y'den bakar: y KUCUK olan izleyiciye yakindir -> levha kirisin onunde.
        const EntityId plateO = add(docO, makeBox({1500, -200, -50}, {2500, -100, 290}, "PL10", "On levha"));
        docO.addWeld(weldOf(beamO, plateO));
        const EntityId asmO = docO.assemblies()[0].id;

        DrawingOptions offO, onO;
        offO.views = {ViewKind::Front};
        onO.views = {ViewKind::Front};
        onO.hiddenLines = true;
        MarkRegistry regOff, regOn;
        const BuildResult bOff = buildSheet(docO, regOff, asmO, offO);
        const BuildResult bOn = buildSheet(docO, regOn, asmO, onO);
        check(bOff.ok && bOn.ok && bOn.sheet.views.size() == 1, "J: okkluzyon cizimi uretildi");
        if (bOff.ok && bOn.ok) {
            const DrawingView& vOff = bOff.sheet.views[0];
            const DrawingView& vOn = bOn.sheet.views[0];
            const auto count = [](const DrawingView& v, LineStyle s) {
                return static_cast<int>(std::count_if(v.segments.begin(), v.segments.end(),
                                                      [&](const Segment2& x) { return x.style == s; }));
            };
            check(count(vOff, LineStyle::Hidden) == 0, "J: gizli cizgi kapali -> Hidden segment yok");
            check(count(vOn, LineStyle::Hidden) >= 2, "J: gizli cizgi acik -> arkadaki kenarlar kesik");
            check(count(vOn, LineStyle::Visible) >= 4, "J: on yuz kenarlari hala duz (gorunur)");

            // KISMI ortulme: ayni yatay kenarda hem gorunur hem gizli parca var.
            const auto horizontalAt = [&](const DrawingView& v, double y, LineStyle s) {
                for (const Segment2& seg : v.segments)
                    if (seg.style == s && std::fabs(seg.a.y - y) < 1e-6 && std::fabs(seg.b.y - y) < 1e-6) return true;
                return false;
            };
            bool splitFound = false;
            for (const Segment2& hid : vOn.segments) {
                if (hid.style != LineStyle::Hidden) continue;
                if (std::fabs(hid.a.y - hid.b.y) > 1e-6) continue;   // yatay gizli parca
                if (horizontalAt(vOn, hid.a.y, LineStyle::Visible)) splitFound = true;
            }
            check(splitFound, "J: kismi ortulme -> ayni kenarda gorunur + gizli parcalar (kenar bolundu)");

            // Gizli parcalar levhanin x araligina yerlesir: kenar tamamen gizlenmez (arka kenar
            // on kenarla ayni cizgiye dusuyorsa gorunur parca kazanir ve gizli parca kirpilir).
            double hidLenBottom = 0.0;
            for (const Segment2& seg : vOn.segments)
                if (seg.style == LineStyle::Hidden && std::fabs(seg.a.y - seg.b.y) < 1e-6)
                    hidLenBottom += std::hypot(seg.b.x - seg.a.x, seg.b.y - seg.a.y);
            check(hidLenBottom > 0.0 && hidLenBottom <= 2200.0,
                  "J: gizli kesikler yalniz levha bolgesinde (toplam ~2 x 1000 mm, 4000 degil)");

            // Geometri korunur: gizli/acik toplam cizgi uzunlugu ayni kalir.
            const auto totalLength = [](const DrawingView& v) {
                double sum = 0.0;
                for (const Segment2& seg : v.segments) sum += std::hypot(seg.b.x - seg.a.x, seg.b.y - seg.a.y);
                return sum;
            };
            const double lOff = totalLength(vOff), lOn = totalLength(vOn);
            check(std::fabs(lOn - lOff) <= lOff * 0.02,
                  "J: gizli cizgi geometriyi degistirmez (toplam uzunluk ~ayni)");
        }
    }

    std::printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
