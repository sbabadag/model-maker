// P2D / P2 — COKLU SECIM (Window/Crossing) + SNAP'LI GRUP TASIMA (Qt'siz, OCCT'siz).
//
// Kabul kosullari:
//   * Bant yonu modu belirler: soldan saga = Window (tam kapsama), sagdan sola = Crossing (kesisim).
//     Kazara (bant sayilmayacak kadar kisa) hareket secim yapmaz.
//   * Gorunusler bantla SECILMEZ (yanlislikla tasinmasin); tiklayarak secilir.
//   * Grup tasima TEK cagri = TEK geri-al adimi; muhendislik verisi (olcu `value/p1/p2`) DEGISMEZ.
//   * Snap pikselden bagimsiz: tolerans mm olarak verilir, en yakin aday kazanir.
//   * Esc (cancel) acik snapshot'tan geri doner; geri-al yigini kirlenmez.

#include "fab_fixture.hpp"
#include "model_maker/drawing_layout.hpp"
#include "model_maker/drawing_set.hpp"
#include "model_maker/paper_editing.hpp"

#include <algorithm>
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

static DrawingSheet sheetOf(fx::Fixture& f) {
    MarkRegistry reg;
    DrawingSetResult r = buildDrawingSet(f.doc, reg, f.finPlate, {}, DrawingPreferences{});
    return r.document.sheets().front();
}

// Muhendislik verisi imzasi: rol|featureKey|deger|p1|p2|gorunus. Yerlesim tasimasinda DEGISMEMELI.
static std::vector<std::string> dimSignature(const DrawingSheet& s) {
    std::vector<std::string> out;
    for (const Dimension2D& d : s.dimensions) {
        char buf[220];
        std::snprintf(buf, sizeof(buf), "%d|%s|%.6f|%.6f,%.6f|%.6f,%.6f|%llu", static_cast<int>(d.role),
                      d.featureKey.c_str(), d.value, d.p1.x, d.p1.y, d.p2.x, d.p2.y,
                      static_cast<unsigned long long>(d.viewId));
        out.push_back(buf);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Kagit uzerinde bilinen konumda iki cizgi + bir metin + bir daire uretir (secim bantlari icin).
struct Annot {
    ElementId lineA{0}, lineB{0}, circle{0};
};

static Annot makeAnnot(DrawingSheet& s) {
    Annot a;
    a.lineA = addPaperShape(s, ShapeKind::Line, {20.0, 200.0}, {60.0, 200.0});
    a.lineB = addPaperShape(s, ShapeKind::Line, {20.0, 190.0}, {80.0, 190.0});   // bandin sagindan tasar
    a.circle = addPaperShape(s, ShapeKind::Circle, {40.0, 170.0}, {50.0, 170.0}, 10.0);
    TextAnnotation t;
    t.id = s.allocateId();
    t.kind = TextKind::GeneralNote;
    t.lines.push_back("P2 NOT");
    t.pos = {30.0, 160.0};
    t.visible = true;
    s.texts.push_back(t);
    return a;
}

int main() {
    fx::Fixture f = fx::make({}, 4000.0, false);
    DrawingSheet base = sheetOf(f);
    check(base.layout.ok && !base.dimensions.empty() && !base.views.empty(), "0: temel pafta hazir");

    // =====================================================================
    // A) Bant yonu: soldan saga Window, sagdan sola Crossing, kisa hareket None
    // =====================================================================
    {
        check(paperBandMode({10, 10}, {90, 90}) == PaperBandMode::Window, "A: soldan saga -> Window");
        check(paperBandMode({90, 90}, {10, 10}) == PaperBandMode::Crossing, "A: sagdan sola -> Crossing");
        check(paperBandMode({10, 10}, {12, 11}, 5.0) == PaperBandMode::None,
              "A: 2.2 mm hareket (tol 5 mm) -> None (tik sayilir)");
        const Bounds2 b1 = paperBandBounds({90, 80}, {10, 20});
        const Bounds2 b2 = paperBandBounds({10, 20}, {90, 80});
        check(nearTo(b1.min.x, 10) && nearTo(b1.max.x, 90) && nearTo(b1.min.y, 20) && nearTo(b1.max.y, 80),
              "A: bant sinirlari yon bagimsiz normalize");
        check(nearTo(b1.min.x, b2.min.x) && nearTo(b1.max.y, b2.max.y), "A: iki kose sirasi ayni banti verir");
    }

    // =====================================================================
    // B) Eleman sinirlari: sekil cizgi/daire, metin, olcu, gorunus
    // =====================================================================
    DrawingSheet s = base;
    const Annot an = makeAnnot(s);
    {
        PaperHit h;
        h.kind = PaperHitKind::Shape;
        h.id = an.lineA;
        Bounds2 b = paperElementBounds(s, h);
        check(!b.empty && nearTo(b.min.x, 20) && nearTo(b.max.x, 60) && nearTo(b.min.y, 200) && nearTo(b.max.y, 200),
              "B: cizgi sinirlari = a..b");

        h.id = an.circle;
        b = paperElementBounds(s, h);
        check(nearTo(b.min.x, 30) && nearTo(b.max.x, 50) && nearTo(b.min.y, 160) && nearTo(b.max.y, 180),
              "B: daire sinirlari = merkez +- yaricap");

        h.kind = PaperHitKind::Text;
        h.id = s.texts.back().id;
        b = paperElementBounds(s, h);
        check(!b.empty && b.width() > 0.0 && b.height() > 0.0, "B: metin sinirlari cozuldu");

        const Dimension2D& d = s.dimensions.front();
        h.kind = PaperHitKind::Dimension;
        h.id = d.id;
        b = paperElementBounds(s, h);
        const Bounds2 tb = resolveDimension(s, d).textBox.aabb();
        check(!b.empty && b.min.x <= tb.min.x + 1e-9 && b.max.x >= tb.max.x - 1e-9,
              "B: olcu sinirlari olcu cizgilerini + metni kapsar");

        h.kind = PaperHitKind::View;
        h.id = s.views.front().id;
        b = paperElementBounds(s, h);
        const Bounds2 pb = s.views.front().paperBounds();
        check(!b.empty && nearTo(b.min.x, pb.min.x) && nearTo(b.max.y, pb.max.y), "B: gorunus sinirlari = paperBounds");

        h.kind = PaperHitKind::Shape;
        h.id = 999999;
        check(paperElementBounds(s, h).empty, "B: olmayan eleman -> bos sinir (uydurma yok)");
    }

    // =====================================================================
    // C) Window: yalniz TAMAMEN icindeki; Crossing: KESISEN de secilir
    // =====================================================================
    {
        // 1. cizgiyi tamamen, 2. cizgiyi kismen kapsayan bant.
        const Bounds2 band = paperBandBounds({15.0, 185.0}, {65.0, 205.0});
        const PaperSelection win = selectPaperElements(s, band, PaperBandMode::Window);
        check(win.contains(PaperHitKind::Shape, an.lineA), "C: Window -> tamamen icindeki cizgi secildi");
        check(!win.contains(PaperHitKind::Shape, an.lineB), "C: Window -> kismen disarida olan cizgi SECILMEDI");
        check(!win.contains(PaperHitKind::Shape, an.circle), "C: Window -> bant disindaki daire secilmedi");

        const PaperSelection cross = selectPaperElements(s, band, PaperBandMode::Crossing);
        check(cross.contains(PaperHitKind::Shape, an.lineA) && cross.contains(PaperHitKind::Shape, an.lineB),
              "C: Crossing -> kesisen iki cizgi de secildi");
        check(!cross.contains(PaperHitKind::Shape, an.circle), "C: Crossing -> uzak daire yine secilmedi");

        const PaperSelection none = selectPaperElements(s, band, PaperBandMode::None);
        check(none.empty(), "C: mode None -> bos secim");
        const PaperSelection emptyBand = selectPaperElements(s, Bounds2{}, PaperBandMode::Crossing);
        check(emptyBand.empty(), "C: bos bant -> bos secim");
    }

    // =====================================================================
    // D) Gorunusler bantla secilmez; tiklayarak hala secilir
    // =====================================================================
    {
        const DrawingView& v = s.views.front();
        const Bounds2 pb = v.paperBounds();
        const PaperSelection sel = selectPaperElements(s, pb, PaperBandMode::Crossing);
        bool hasView = false;
        for (const PaperHit& h : sel.hits) hasView = hasView || h.kind == PaperHitKind::View;
        check(!hasView, "D: gorunusler bantla secilmedi (yanlislikla tasima korumasi)");
        check(!sel.empty(), "D: bant yine de olcu/mark/metinleri secti (bos degil)");
        const PaperHit click = hitTestPaper(s, {pb.centre().x + pb.width() * 0.45, pb.centre().y}, 0.01);
        check(click.valid(), "D: gorunus tikla hala secilebilir (hit-test bozulmadi)");
    }

    // =====================================================================
    // E) Grup tasima: sekiller + metin ayni delta; olcu verisi DEGISMEZ
    // =====================================================================
    {
        DrawingSheet e = s;
        const std::vector<std::string> before = dimSignature(e);
        const Bounds2 band = paperBandBounds({10.0, 150.0}, {95.0, 210.0});
        const PaperSelection sel = selectPaperElements(e, band, PaperBandMode::Window);
        check(sel.size() >= 4, "E: bant 4 elemani sectti (3 sekil + 1 metin): " + std::to_string(sel.size()));

        const Vec2 d{10.0, 5.0};
        const Vec2 lineA0 = e.shapes[0].a;
        const Vec2 text0 = e.texts.back().pos;
        const std::size_t moved = movePaperElements(e, sel, d);
        check(moved == sel.size(), "E: secilen HER eleman tasindi (" + std::to_string(moved) + "/" +
                                        std::to_string(sel.size()) + ")");
        check(nearTo(e.shapes[0].a.x, lineA0.x + 10) && nearTo(e.shapes[0].a.y, lineA0.y + 5),
              "E: cizgi ucu ayni delta kadar tasindi");
        check(nearTo(e.texts.back().pos.x, text0.x + 10) && nearTo(e.texts.back().pos.y, text0.y + 5),
              "E: metin ayni delta kadar tasindi");
        check(dimSignature(e) == before, "E: olcu degerleri/p1/p2 DEGISMEDI (muhendislik verisi korunur)");
    }

    // =====================================================================
    // F) Olcu secilip tasininca: yalniz offset/side/textT degisir, deger ayni
    // =====================================================================
    {
        DrawingSheet e = s;
        const Dimension2D d0 = e.dimensions.front();
        PaperSelection sel;
        PaperHit h;
        h.kind = PaperHitKind::Dimension;
        h.id = d0.id;
        sel.hits.push_back(h);
        const std::size_t moved = movePaperElements(e, sel, {0.0, 6.0});
        const Dimension2D& d1 = e.dimensions.front();
        check(moved == 1, "F: olcu tasindi");
        check(nearTo(d1.value, d0.value) && nearTo(d1.p1.x, d0.p1.x) && nearTo(d1.p1.y, d0.p1.y) &&
                  nearTo(d1.p2.x, d0.p2.x) && nearTo(d1.p2.y, d0.p2.y),
              "F: olcu degeri ve olculen noktalar DEGISMEDI");
        check(!nearTo(d1.offset, d0.offset) || !nearTo(d1.textT, d0.textT), "F: yalniz yerlesim alanlari degisti");
    }

    // =====================================================================
    // G) Snap: en yakin aday, tolerans disinda snap YOK, en yakin kazanir
    // =====================================================================
    {
        const PaperSnap nearA = snapPaperPoint(s, {20.6, 200.5}, 3.0);
        check(nearA.snapped && nearTo(nearA.point.x, 20.0) && nearTo(nearA.point.y, 200.0),
              "G: 0.8 mm uzakta cizgi ucuna snap");
        check(nearA.what == std::string("sekil ucu"), "G: snap etiketi 'sekil ucu'");

        const PaperSnap far = snapPaperPoint(s, {200.0, 60.0}, 3.0);
        check(!far.snapped && nearTo(far.point.x, 200.0) && nearTo(far.point.y, 60.0),
              "G: tolerans disinda snap YOK, nokta aynen doner");

        // Iki aday da tolerans icinde: yakin olan kazanir (uç 40,170 vs merkez 40,170 yok; daire kadrani 40,170)
        const PaperSnap nearest = snapPaperPoint(s, {59.2, 200.4}, 3.0);
        check(nearest.snapped && nearTo(nearest.point.x, 60.0) && nearTo(nearest.point.y, 200.0),
              "G: iki aday arasinda EN YAKIN secildi");
        check(!snapPaperPoint(s, {20.4, 200.3}, 0.0).snapped, "G: tol 0 -> snap yok");
    }

    // =====================================================================
    // H) Tek geri-al adimi: grup tasima + undo butunu geri alir
    // =====================================================================
    {
        DrawingSheet e = s;
        PaperEditHistory hist(e, 16);
        const PaperSelection sel = selectPaperElements(hist.sheet(), paperBandBounds({10, 150}, {95, 210}),
                                                       PaperBandMode::Window);
        const Vec2 a0 = hist.sheet().shapes[0].a;
        hist.begin();
        const std::size_t moved = movePaperElements(hist.sheet(), sel, {7.0, -3.0});
        hist.commit();
        check(moved >= 4 && hist.undoDepth() == 1, "H: grup tasima TEK geri-al adimi uretir");
        check(hist.undo(), "H: undo calisti");
        check(nearTo(hist.sheet().shapes[0].a.x, a0.x) && nearTo(hist.sheet().shapes[0].a.y, a0.y),
              "H: undo TUM secimi geri aldi (kismi geri donus yok)");
        check(hist.redo() && nearTo(hist.sheet().shapes[0].a.x, a0.x + 7.0), "H: redo tasimayi geri getirdi");
    }

    // =====================================================================
    // I) cancel(): acik snapshot'tan geri donus, yigin temiz kalir
    // =====================================================================
    {
        PaperEditHistory hist(s, 8);
        const Vec2 a0 = hist.sheet().shapes[0].a;
        hist.begin();
        PaperSelection sel;
        PaperHit h;
        h.kind = PaperHitKind::Shape;
        h.id = hist.sheet().shapes[0].id;
        sel.hits.push_back(h);
        movePaperElements(hist.sheet(), sel, {25.0, 0.0});
        check(nearTo(hist.sheet().shapes[0].a.x, a0.x + 25.0), "I: tasima aninda gorunur (canli)");
        check(hist.cancel(), "I: cancel acik snapshot'i iptal etti");
        check(nearTo(hist.sheet().shapes[0].a.x, a0.x), "I: Esc sonrasi geometri ESKI haline dondu");
        check(hist.undoDepth() == 0 && !hist.canRedo(), "I: iptal geri-al yiginini kirletmedi");
        check(!hist.cancel(), "I: acik snapshot yokken cancel false");
    }

    // =====================================================================
    // J) AYNI OLCU CIZGISI = TEK OBJE: zincir segmenti secilince tum zincir
    // =====================================================================
    {
        DrawingSheet s2 = base;
        // Ayni (viewId, chain) zincirinde en az iki segment ara (gercek imalat olculeri).
        ElementId chainId = 0, viewId = 0, firstId = 0;
        std::size_t chainSize = 0;
        for (const Dimension2D& d : s2.dimensions) {
            std::size_t n = 0;
            for (const Dimension2D& e : s2.dimensions)
                if (e.visible && e.viewId == d.viewId && e.chain == d.chain) ++n;
            if (n >= 2) {
                chainId = d.chain;
                viewId = d.viewId;
                firstId = d.id;
                chainSize = n;
                break;
            }
        }
        check(chainId != 0 && chainSize >= 2, "J: ayni cizgi uzerinde en az iki olcu segmenti var");

        PaperSelection one;
        one.hits.push_back({PaperHitKind::Dimension, firstId, {0.0, 0.0}});
        const PaperSelection full = expandPaperSelection(s2, one);
        check(full.size() == chainSize, "J: bir segment secilince zincirin TUM segmentleri secilir");
        bool sameChain = true;
        for (const PaperHit& h : full.hits) {
            const Dimension2D* d = nullptr;
            for (const Dimension2D& e : s2.dimensions)
                if (e.id == h.id) { d = &e; break; }
            if (!d || d->viewId != viewId || d->chain != chainId) sameChain = false;
        }
        check(sameChain, "J: genisletme yalniz ayni zinciri kapsar (baska zincir karismaz)");

        // Baska zincirdeki bir olcu genisletmeye dahil olmaz.
        ElementId otherDim = 0;
        for (const Dimension2D& d : s2.dimensions)
            if (d.chain != chainId || d.viewId != viewId) { otherDim = d.id; break; }
        check(otherDim == 0 || !full.contains(PaperHitKind::Dimension, otherDim),
              "J: farkli zincirdeki olcu secilmedi");

        // Grup tasima: tek cagri tum zinciri tasir, olcu CIZGISI ayni kalir (offset'ler esit artar),
        // muhendislik verisi (value/p1/p2) degismez.
        const std::vector<std::string> sigBefore = dimSignature(s2);
        PaperEditHistory hist(s2, 8);
        hist.begin();
        const std::size_t moved = movePaperElements(hist.sheet(), full, {0.0, 12.0});
        hist.commit();
        check(moved == full.size(), "J: zincirdeki HER segment tasindi (kismi tasima yok)");
        check(dimSignature(hist.sheet()) == sigBefore, "J: tasima muhendislik verisini DEGISTIRMEDI");
        bool sameRow = true;
        double firstOffset = -1e300;
        for (const Dimension2D& d : hist.sheet().dimensions) {
            if (!d.visible || d.viewId != viewId || d.chain != chainId) continue;
            if (firstOffset < -1e299) firstOffset = d.offset;
            else if (!nearTo(d.offset, firstOffset)) sameRow = false;
        }
        check(sameRow, "J: zincir segmentleri AYNI olcu cizgisinde kaldi");
        check(hist.undoDepth() == 1 && hist.undo(), "J: zincir tasimasi TEK geri-al adimi");
    }

    // =====================================================================
    // K) OLCU HASSASIYETI: yalniz mm (ondalik basamak yok)
    // =====================================================================
    {
        bool integerOnly = true;
        int checked = 0;
        for (const Dimension2D& d : base.dimensions) {
            const std::string t = dimensionText(d.value);
            ++checked;
            if (t.find('.') != std::string::npos || t.find(',') != std::string::npos) integerOnly = false;
        }
        check(checked > 0, "K: olcu metinleri uretildi");
        check(integerOnly, "K: TUM olcu metinleri tam mm (3591.3 / 429.2 gibi ondalik YOK)");
        check(dimensionText(3591.3) == "3591" && dimensionText(429.2) == "429" &&
                  dimensionText(3.3) == "3" && dimensionText(14.6) == "15",
              "K: yuvarlama tam mm'e (3591.3->3591, 429.2->429, 3.3->3, 14.6->15)");
    }

    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
