// P2B — PAFTADA 2B DUZENLEME CEKIRDEGI (Qt'siz, OCCT'siz).
// Kapsam: hit-test, tasima (olcu/gorunus/mark/metin/sekil), 2B sekil ekleme, gorunurluk -> INCOMPLETE,
// geri al/yinele, JSON kaliciligi, model yeniden uretiminde kullanici yerlesiminin korunmasi.

#include "fab_fixture.hpp"
#include "model_maker/drawing_layout.hpp"
#include "model_maker/drawing_set.hpp"
#include "model_maker/paper_editing.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mm;
using namespace mm::drawing;

static int failures = 0;
static int checks = 0;
static void check(bool ok, const std::string& label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
static bool nearTo(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

static DrawingSheet singleSheet(fx::Fixture& f) {
    MarkRegistry reg;
    DrawingSetResult r = buildDrawingSet(f.doc, reg, f.finPlate, {}, DrawingPreferences{});
    return r.document.sheets().front();
}

static const Dimension2D* dimById(const DrawingSheet& s, ElementId id) {
    for (const Dimension2D& d : s.dimensions)
        if (d.id == id) return &d;
    return nullptr;
}

// DrawingSheet tek basina serialize etmez; kalicilik kabi DrawingDocument'tur (tek model).
static std::string sheetJson(const DrawingSheet& s) {
    DrawingDocument d;
    d.addSheet(s);
    return d.serialize();
}

int main() {
    fx::Fixture f = fx::make({}, 4000.0, false);
    const DrawingSheet base = singleSheet(f);
    check(base.layout.ok && !base.dimensions.empty() && !base.views.empty(), "A: temel tek-pafta cizimi hazir");

    // ---------------------------------------------------------------------
    // A) Hit-test: olcu / gorunus / metin / bos alan + tolerans
    // ---------------------------------------------------------------------
    DrawingSheet s = base;
    {
        const Dimension2D& d = s.dimensions.front();
        const DimensionGeometry g = resolveDimension(s, d);
        check(g.valid, "A: olcu geometrisi cozuldu");
        PaperHit hit = hitTestPaper(s, g.textCentre, 0.5);
        check(hit.kind == PaperHitKind::Dimension && hit.id == d.id, "A: olcu metnine tik -> Dimension (dogru id)");

        const DrawingView& v = s.views.front();
        const Bounds2 pb = v.paperBounds();
        hit = PaperHit{};
        for (int i = 1; i <= 9 && !hit.valid(); ++i)
            for (int j = 1; j <= 9 && !hit.valid(); ++j) {
                const Vec2 p{pb.min.x + pb.width() * (0.05 * i), pb.min.y + pb.height() * (0.05 * j)};
                const PaperHit cand = hitTestPaper(s, p, 0.01);
                if (cand.kind == PaperHitKind::View) hit = cand;
            }
        check(hit.kind == PaperHitKind::View && hit.id == v.id, "A: gorunus icinde bos nokta -> View");

        bool textHit = false;
        for (const TextAnnotation& t : s.texts) {
            const TextGeometry tg = resolveText(s, t);
            const PaperHit th = hitTestPaper(s, tg.box.c, 0.5);
            if (th.kind == PaperHitKind::Text && th.id == t.id) textHit = true;
        }
        check(textHit, "A: metin kutusuna tik -> Text");

        hit = hitTestPaper(s, {-500.0, -500.0}, 1.0);
        check(hit.kind == PaperHitKind::None, "A: sayfa disi nokta -> None (yanlis secim yok)");

        const LineSeg& ln = g.lines.back();
        const Vec2 mid{(ln.a.x + ln.b.x) / 2.0, (ln.a.y + ln.b.y) / 2.0};
        const Vec2 off{mid.x, mid.y + 1.0};
        check(hitTestPaper(s, off, 1.5).kind == PaperHitKind::Dimension &&
                  hitTestPaper(s, off, 0.05).kind != PaperHitKind::Dimension,
              "A: tolerans kagit mm cinsinden calisiyor (1 mm fark, 1.5 mm tol -> isabet)");
    }

    // ---------------------------------------------------------------------
    // B) 2B sekil ekleme + JSON kaliciligi + kanit (provenance)
    // ---------------------------------------------------------------------
    {
        const ElementId l1 = addPaperShape(s, ShapeKind::Line, {30, 30}, {120, 30});
        const ElementId c1 = addPaperShape(s, ShapeKind::Circle, {60, 60}, {80, 60}, 20.0);
        const ElementId cl = addPaperShape(s, ShapeKind::Centerline, {30, 90}, {150, 90});
        check(l1 != c1 && c1 != cl && l1 != 0, "B: sekiller tekil ElementId aldi");
        check(s.shapes.size() == 3 && s.shapes[0].featureKey == "user:line" && s.shapes[1].featureKey == "user:circle" &&
                  s.shapes[2].featureKey == "user:centerline",
              "B: sekil turu/provenance kaydedildi");
        check(nearTo(s.shapes[1].radius, 20.0), "B: daire yaricapi korunuyor");

        DrawingDocument doc;
        s.role = SheetRole::Primary;
        s.sheetNumber = "T1";
        doc.addSheet(s);
        doc.setPreferences(DrawingPreferences{});
        const std::string json = doc.serialize();
        DrawingDocument back;
        std::string err;
        check(DrawingDocument::deserialize(json, back, err), "B: JSON okundu (" + err + ")");
        check(back.sheets().size() == 1 && back.sheets()[0].shapes.size() == 3 &&
                  back.sheets()[0].shapes[1].id == c1 && nearTo(back.sheets()[0].shapes[2].b.x, 150.0),
              "B: sekiller (id/geometri) JSON turunda birebir korunur");
        check(back.serialize() == json, "B: serialize -> deserialize -> serialize ayni");
    }

    // ---------------------------------------------------------------------
    // C) Tasima: olcu degeri DEGISMEZ; gorunus/mark/metin/sekil tasinir
    // ---------------------------------------------------------------------
    {
        PaperEditHistory h(base);
        const Dimension2D& d0 = h.sheet().dimensions.front();
        const double value0 = d0.value, p1x = d0.p1.x, p2x = d0.p2.x;
        const double offset0 = d0.offset;
        const DimensionGeometry g0 = resolveDimension(h.sheet(), d0);
        const PaperHit hit = hitTestPaper(h.sheet(), g0.textCentre, 0.5);
        check(hit.kind == PaperHitKind::Dimension, "C: tasinacak olcu secildi");
        h.begin();
        check(movePaperElement(h.sheet(), hit, {0.0, -8.0}), "C: olcu tasindi");
        h.commit();
        const Dimension2D& d1 = *dimById(h.sheet(), d0.id);
        check(!nearTo(d1.offset, offset0), "C: olcu cizgisi uzakligi degisti (kullanici yerlesimi)");
        check(nearTo(d1.value, value0) && nearTo(d1.p1.x, p1x) && nearTo(d1.p2.x, p2x),
              "C: MUHENDISLIK DEGERI degismedi (value/p1/p2 sabit)");
        check(nearTo(d1.measured(), d1.value, 1e-6), "C: measured() == value (tutarlilik)");

        // Gorunus tasima: kagida izdusum deltasi kadar kayar, model degismez.
        const DrawingView& v0 = h.sheet().views.front();
        const Vec2 vOrigin = v0.origin;
        const Vec2 local = v0.bounds.centre();
        const Vec2 before = v0.toPaper(local);
        const PaperHit vh = [&]() {
            const Bounds2 pb = h.sheet().views.front().paperBounds();
            for (int i = 1; i <= 9; ++i)
                for (int j = 1; j <= 9; ++j) {
                    const Vec2 p{pb.min.x + pb.width() * (0.05 * i), pb.min.y + pb.height() * (0.05 * j)};
                    const PaperHit cand = hitTestPaper(h.sheet(), p, 0.01);
                    if (cand.kind == PaperHitKind::View) return cand;
                }
            return PaperHit{};
        }();
        h.begin();
        check(vh.kind == PaperHitKind::View && movePaperElement(h.sheet(), vh, {12.0, 5.0}), "C: gorunus secildi ve tasindi");
        h.commit();
        const DrawingView& v1 = h.sheet().views.front();
        const Vec2 after = v1.toPaper(local);
        check(nearTo(after.x - before.x, 12.0) && nearTo(after.y - before.y, 5.0),
              "C: tum gorunus geometrisi kagit mm delta kadar kaydi");
        check(nearTo(v1.origin.x, vOrigin.x + 12.0), "C: gorunus origin guncellendi");

        // Mark etiketi
        if (!h.sheet().marks.empty()) {
            const MarkAnnotation& m0 = h.sheet().marks.front();
            const Vec2 pos0 = m0.labelPos;
            const PaperHit mh = hitTestPaper(h.sheet(), markBox(m0).centre(), 0.5);
            h.begin();
            const bool moved = mh.kind == PaperHitKind::Mark && movePaperElement(h.sheet(), mh, {-6.0, 4.0});
            h.commit();
            const MarkAnnotation& m1 = h.sheet().marks.front();
            check(moved && nearTo(m1.labelPos.x, pos0.x - 6.0) && nearTo(m1.labelPos.y, pos0.y + 4.0),
                  "C: mark etiketi tasindi (metin/geometri sabit)");
        }

        // Metin (sayfa notu)
        if (!h.sheet().texts.empty()) {
            const TextAnnotation& t0 = h.sheet().texts.front();
            const TextGeometry tg = resolveText(h.sheet(), t0);
            const PaperHit th = hitTestPaper(h.sheet(), tg.box.c, 0.5);
            const Vec2 pos0 = t0.pos;
            h.begin();
            const bool moved = th.kind == PaperHitKind::Text && movePaperElement(h.sheet(), th, {3.0, -2.0});
            h.commit();
            const TextAnnotation& t1 = h.sheet().texts.front();
            check(moved && nearTo(t1.pos.x, pos0.x + 3.0) && nearTo(t1.pos.y, pos0.y - 2.0),
                  "C: metin notu tasindi");
        }

        // Sekil tasima
        const ElementId sid = addPaperShape(h.sheet(), ShapeKind::Line, {10, 10}, {60, 10});
        const PaperHit shHit = hitTestPaper(h.sheet(), {35.0, 10.0}, 1.0);
        h.begin();
        check(shHit.kind == PaperHitKind::Shape && shHit.id == sid && movePaperElement(h.sheet(), shHit, {5.0, 7.0}),
              "C: kullanici sekli secildi ve tasindi");
        h.commit();
        check(nearTo(h.sheet().shapes.back().a.x, 15.0) && nearTo(h.sheet().shapes.back().b.y, 17.0),
              "C: sekil uclari delta kadar kaydi");
    }

    // ---------------------------------------------------------------------
    // D) Geri al / yinele
    // ---------------------------------------------------------------------
    {
        PaperEditHistory h(base);
        const std::string initial = sheetJson(h.sheet());
        const DimensionGeometry g0 = resolveDimension(h.sheet(), h.sheet().dimensions.front());
        const PaperHit hit = hitTestPaper(h.sheet(), g0.textCentre, 0.5);
        const double off0 = h.sheet().dimensions.front().offset;

        h.begin();
        movePaperElement(h.sheet(), hit, {0.0, -6.0});
        h.commit();
        const std::string afterMove = sheetJson(h.sheet());
        check(h.canUndo() && !h.canRedo() && h.undoDepth() == 1, "D: bir duzenleme geri alinabilir");
        check(h.undo() && sheetJson(h.sheet()) == initial, "D: geri al -> pafta birebir ilk haline dondu");
        check(h.canRedo() && h.redo() && sheetJson(h.sheet()) == afterMove, "D: yinele -> duzenleme geri geldi");
        check(nearTo(h.sheet().dimensions.front().offset, off0 - 6.0) ||
                  !nearTo(h.sheet().dimensions.front().offset, off0),
              "D: yinelennen duzenleme olcu uzakligini degistirdi");

        h.begin();
        h.commit();
        check(h.undoDepth() == 1, "D: degisiklik yoksa gecmis kaydi ACILMAZ (bos adim yok)");
        h.begin();
        addPaperShape(h.sheet(), ShapeKind::Circle, {40, 40}, {60, 40}, 10.0);
        h.commit();
        check(h.undoDepth() == 2 && h.undo(), "D: ikinci duzenleme ayri adim");
    }

    // ---------------------------------------------------------------------
    // E) Gizleme -> INCOMPLETE (sahte READY yok), bagimsiz olcum gizliyi saymaz
    // ---------------------------------------------------------------------
    {
        DrawingSheet g = base;
        check(!g.fabrication.draft, "E: temel cizim TASLAK degil");
        const OverlapReport clean = measureOverlaps(g);
        check(clean.total() == 0, "E: temel olcum temiz");

        const Dimension2D* fab = nullptr;
        for (const Dimension2D& d : g.dimensions)
            if (!d.featureKey.empty()) fab = &d;
        check(fab != nullptr, "E: imalat kaynakli olcu var");
        const ElementId fabId = fab->id;
        const DimensionGeometry fg = resolveDimension(g, *fab);
        const PaperHit hit = hitTestPaper(g, fg.textCentre, 0.5);
        check(hit.kind == PaperHitKind::Dimension && hit.id == fabId, "E: imalat olcusu secildi");
        check(setPaperElementVisible(g, hit, false), "E: imalat olcusu gizlendi");
        evaluateAnnotationStatus(g);
        check(g.fabrication.draft, "E: zorunlu imalat olcusu gizlenince cizim TASLAK/INCOMPLETE");
        bool itemUnresolved = false;
        for (const ChecklistItem& c : g.fabrication.checklist)
            if (c.key == "ANNOTATIONS" && c.state == ChecklistState::Unresolved) itemUnresolved = true;
        check(itemUnresolved, "E: kontrol listesinde ANNOTATIONS unresolved");
        bool code = false;
        for (const std::string& c : g.fabrication.unresolvedCodes) code = code || c == "ANNOTATION_INCOMPLETE";
        check(code, "E: unresolvedCodes ANNOTATION_INCOMPLETE");
        check(measureOverlaps(g).total() == 0, "E: gizli oge cakisma olarak SAYILMAZ (yanlis NO_FIT yok)");
        check(hitTestPaper(g, fg.textCentre, 0.5).kind != PaperHitKind::Dimension, "E: gizli olcu artik secilemez");

        // Geri getir -> TAM
        for (Dimension2D& d : g.dimensions)
            if (d.id == fabId) d.visible = true;
        evaluateAnnotationStatus(g);
        check(!g.fabrication.draft, "E: olcu geri getirilince TASLAK kalkti");
    }

    // ---------------------------------------------------------------------
    // F) Model yeniden uretimi: kullanici yerlesimi korunur, degerler modelden
    // ---------------------------------------------------------------------
    {
        const std::uint64_t rev0 = f.doc.revision();
        DrawingSheet edited = base;
        // olcu, gorunus, metin ve 2B sekil duzenle
        const DimensionGeometry dg = resolveDimension(edited, edited.dimensions.front());
        PaperHit dh = hitTestPaper(edited, dg.textCentre, 0.5);
        movePaperElement(edited, dh, {0.0, -7.0});
        const double editedOffset = edited.dimensions.front().offset;
        const DrawingView& ev = edited.views.front();
        PaperHit vh = hitTestPaper(edited, ev.paperBounds().centre(), 0.01);
        movePaperElement(edited, vh, {9.0, 3.0});
        const Vec2 editedOrigin = edited.views.front().origin;
        if (!edited.texts.empty()) movePaperElement(edited, hitTestPaper(edited, resolveText(edited, edited.texts.front()).box.c, 0.5), {2.0, 2.0});
        const Vec2 editedTextPos = edited.texts.empty() ? Vec2{} : edited.texts.front().pos;
        const ElementId shapeId = addPaperShape(edited, ShapeKind::Line, {20, 20}, {60, 20});

        MarkRegistry reg;
        DrawingSetResult rr = buildDrawingSet(f.doc, reg, f.finPlate, {}, DrawingPreferences{});
        DrawingSheet regenerated = rr.document.sheets().front();
        preservePaperEdits(edited, regenerated);
        check(nearTo(regenerated.dimensions.front().offset, editedOffset), "F: olcu yerlesimi yeniden uretimde korundu");
        check(nearTo(regenerated.views.front().origin.x, editedOrigin.x) &&
                  nearTo(regenerated.views.front().origin.y, editedOrigin.y),
              "F: gorunus konumu korundu");
        if (!edited.texts.empty()) check(nearTo(regenerated.texts.front().pos.x, editedTextPos.x),
                                         "F: not konumu korundu");
        check(regenerated.shapes.size() == 1 && regenerated.shapes.front().id == shapeId, "F: 2B sekil (ayni ElementId) korundu");
        check(regenerated.nextId > shapeId, "F: nextId yeni sekille cakismayacak sekilde guncellendi");
        bool valuesSame = true;
        for (const Dimension2D& d : regenerated.dimensions) valuesSame = valuesSame && nearTo(d.measured(), d.value, 1e-6);
        check(valuesSame, "F: yeniden uretilen olculer GERCEK model degerini tasiyor (measured==value)");
        check(f.doc.revision() == rev0, "F: model belgesi degismedi");
    }

    // ---------------------------------------------------------------------
    // G) Kalicilik: kaydet/yukle sonrasi duzenlemeler ve hit-test ayni
    // ---------------------------------------------------------------------
    {
        DrawingSheet edited = base;
        addPaperShape(edited, ShapeKind::Circle, {50, 50}, {70, 50}, 20.0);
        const DimensionGeometry dg = resolveDimension(edited, edited.dimensions.front());
        movePaperElement(edited, hitTestPaper(edited, dg.textCentre, 0.5), {0.0, -5.0});
        DrawingDocument doc;
        doc.addSheet(edited);
        const std::string json = doc.serialize();
        DrawingDocument back;
        std::string err;
        check(DrawingDocument::deserialize(json, back, err), "G: duzenlenmis pafta JSON'dan okundu");
        const DrawingSheet& b = back.sheets().front();
        check(b.shapes.size() == 1 && nearTo(b.shapes.front().radius, 20.0), "G: sekil kalici");
        check(nearTo(b.dimensions.front().offset, edited.dimensions.front().offset), "G: olcu yerlesimi kalici");
        const PaperHit h2 = hitTestPaper(b, {50.0, 70.0}, 1.0);
        check(h2.kind == PaperHitKind::Shape && h2.id == b.shapes.front().id, "G: yuklenen paftada hit-test calisiyor");
        check(back.serialize() == json, "G: tur birebir");
    }

    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
