// P2C — SMART SHEET MANAGER (OCCT'siz, Qt'siz, Linux ctest).
//
// Kapsam:
//   * Tercihler (auto / single / autoMultiSheetFallback) JSON'da korunur.
//   * Tek pafta: once A3, gerekirse A2/A1/A0 (kagit buyutme) — kucuk yazi/eksik olcu YOK.
//   * Tek pafta kaliteyi saglamiyorsa: ana + numarali devam/detay paftalari, cift yonlu referans,
//     ana paftada indeks, ortak revizyon/antet, deterministik cikti.
//   * Hicbir gecerli duzen yoksa: acik NO_FIT/sorun listesi, READY DEGIL.
//   * Olcu DEGERLERI modelden gelir; pafta bolme degeri degistirmez; Document DEGISMEZ.

#include "fab_fixture.hpp"
#include "model_maker/drawing_layout.hpp"
#include "model_maker/drawing_set.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
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
static bool nearTo(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

// Uzun kiris + oteli, delikli, kaynakli ek levhalar (gercek montaj; imalat envanteri tam).
static fx::Fixture makeAssembly(double len, int extraPlates) {
    fx::Fixture f = fx::make({}, len, true);
    for (int i = 0; i < extraPlates; ++i) {
        const double x = 200.0 + 130.0 * i;
        const EntityId p = fx::addFramedPlate(f, {x, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 300, 200, 10,
                                              ("set_levha_" + std::to_string(i)).c_str());
        fx::addBolts(f, p, {x - 100, 80, 30}, {0, 1, 0}, {1, 0, 0}, 3, 2, 100, 60, 22);
        Weld w;
        w.mainPart = f.beam;
        w.secondaryPart = p;
        w.weldClass = WeldClass::Shop;
        f.doc.addWeld(w);
    }
    return f;
}
static EntityId firstAssembly(const fx::Fixture& f) { return f.doc.assemblies().empty() ? 0 : f.doc.assemblies()[0].id; }

static std::vector<std::string> dimSignature(const DrawingSheet& s) {
    std::vector<std::string> out;
    for (const Dimension2D& d : s.dimensions) {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "%d|%s|%.4f", static_cast<int>(d.role), d.featureKey.c_str(), d.value);
        out.push_back(buf);
    }
    std::sort(out.begin(), out.end());
    return out;
}
static std::vector<std::string> setSignature(const DrawingDocument& doc) {
    std::vector<std::string> out;
    for (const DrawingSheet& s : doc.sheets())
        for (std::string& v : dimSignature(s)) out.push_back(v);
    std::sort(out.begin(), out.end());
    return out;
}
static const DrawingSheet* findSheet(const DrawingDocument& doc, ElementId id) {
    for (const DrawingSheet& s : doc.sheets())
        if (s.id == id) return &s;
    return nullptr;
}
static bool allSheetsPlaced(const DrawingDocument& doc) {
    bool ok = true;
    for (const DrawingSheet& s : doc.sheets()) ok = ok && s.layout.ok && measureOverlaps(s).total() == 0;
    return ok;
}
static bool hasIssue(const std::vector<std::string>& issues, const std::string& needle) {
    for (const std::string& i : issues)
        if (i.find(needle) != std::string::npos) return true;
    return false;
}

int main() {
    // =========================================================================
    // A) Varsayilanlar ve JSON turu (auto / single / true) — kabul testi 1
    // =========================================================================
    {
        const DrawingPreferences defaults{};
        // 2026-10-29 kullanici istegi: otomatik pafta bolme VARSAYILAN KAPALI, tek pafta tasma modu ACIK.
        check(defaults.autoSheetSize && defaults.mode == DrawingMode::Single && !defaults.autoMultiSheetFallback &&
                  defaults.allowOverflow,
              "A: varsayilanlar auto / single / bolme kapali / tasma modu acik");
        check(defaults.autoFirst == SheetSize::A3 && defaults.autoMax == SheetSize::A0,
              "A: otomatik kagit araligi A3 -> A0 (yapilandirilabilir)");

        fx::Fixture f = makeAssembly(1800.0, 0);
        MarkRegistry reg;
        DrawingSetResult r = buildDrawingSet(f.doc, reg, firstAssembly(f), {}, defaults);
        check(r.ok && r.singleSheet && r.document.sheets().size() == 1, "A: varsayilanlarla tek pafta");

        const std::string json = r.document.serialize();
        DrawingDocument back;
        std::string err;
        check(DrawingDocument::deserialize(json, back, err) && err.empty(), "A: JSON v3 okundu");
        check(back.preferences() == r.document.preferences(), "A: tercihler (auto/single/true) deserialize turunda AYNEN korunur");
        check(back.setInfo().present && back.setInfo().id == r.document.setInfo().id &&
                  back.setInfo().primarySheetId == r.document.setInfo().primarySheetId &&
                  back.setInfo().layoutValid == r.document.setInfo().layoutValid &&
                  back.setInfo().ready == r.document.setInfo().ready,
              "A: DrawingSet kimligi/durumu JSON'da korunur");
        check(back.serialize() == json, "A: serialize -> deserialize -> serialize birebir ayni (deterministik)");

        // Kullanici tercihi (sabit A2 + yedek kapali) de kaydedilir.
        DrawingPreferences fixed;
        fixed.autoSheetSize = false;
        fixed.sheetSize = SheetSize::A2;
        fixed.autoMultiSheetFallback = false;
        DrawingDocument d2;
        DrawingSheet s;
        s.name = "TEST";
        s.sheetNumber = "TEST";
        d2.setPreferences(fixed);
        d2.addSheet(std::move(s));
        DrawingDocument d3;
        check(DrawingDocument::deserialize(d2.serialize(), d3, err) && d3.preferences() == fixed,
              "A: sabit kagit + yedek kapali tercihi turda korunur");
    }

    // =========================================================================
    // B) Sade tek parca -> tek okunakli pafta, READY, olcu degerleri gercek
    // =========================================================================
    {
        fx::Fixture g = fx::make({}, 4000.0, false);
        const std::uint64_t revBefore = g.doc.revision();
        const std::size_t modelsBefore = g.doc.models().size();
        MarkRegistry reg;
        DrawingSetResult r = buildDrawingSet(g.doc, reg, g.finPlate, {}, DrawingPreferences{});
        check(r.ok && r.singleSheet && r.document.sheets().size() == 1, "B: tek levha -> tek pafta (coklu sayfa YOK)");
        const DrawingSheet& s = r.document.sheets().front();
        check(s.role == SheetRole::Primary && s.parentSheetId == 0 && !s.sheetNumber.empty(),
              "B: tek pafta rolu primary, kimlik/number atanmis");
        check(s.layout.ok && measureOverlaps(s).total() == 0, "B: yerlesim basarili, bagimsiz olcumde cakisma 0");
        check(s.layoutConfig.dimTextHeight >= 3.0 && s.layoutConfig.noteTextHeight >= 3.5,
              "B: yazi yuksekligi okunabilir minimumun altina inilmedi (>=3.0 / >=3.5)");
        check(r.layoutValid && r.ready, "B: desteklenen kapsam + gecerli yerlesim -> READY");
        check(!s.fabrication.draft, "B: imalat verisi tam (TASLAK degil)");
        check(s.documentRevision == revBefore, "B: sheet.documentRevision = model surumu");
        check(g.doc.revision() == revBefore && g.doc.models().size() == modelsBefore,
              "B: cizim/pafta uretimi Document'i DEGISTIRMEDI");
        bool measured = true;
        for (const Dimension2D& d : s.dimensions) measured = measured && nearTo(d.measured(), d.value, 1e-6);
        check(measured && !s.dimensions.empty(), "B: her olcude measured() == value (uydurma 2B olcu yok)");
    }

    // =========================================================================
    // C) Orta montaj -> kagit buyuterek TEK pafta (coklu sayfa uretilmez)
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(1800.0, 0);
        MarkRegistry reg;
        DrawingSetResult r = buildDrawingSet(f.doc, reg, firstAssembly(f), {}, DrawingPreferences{});
        const DrawingSheet& s = r.document.sheets().front();
        check(r.ok && r.singleSheet && r.document.sheets().size() == 1, "C: orta montaj tek paftada cozuldu");
        check(s.layout.ok && measureOverlaps(s).total() == 0 && s.format.size >= SheetSize::A2,
              "C: A3 yetmedi -> daha buyuk kagit (>=A2), cakisma 0");
        check(s.layout.requestedSheet == SheetSize::A3 && s.format.size == s.layout.finalSheet,
              "C: istenen A3, nihai kagit raporla tutarli");
        check(r.layoutValid, "C: set gecerliligi true");
        bool draftItem = false;
        for (const ChecklistItem& c : s.fabrication.checklist)
            if (c.key == "WELDS" && c.state == ChecklistState::Unresolved) draftItem = true;
        check(draftItem && s.fabrication.draft && !r.ready,
              "C: kaynak olcusu desteklenmiyor -> TASLAK/INCOMPLETE, READY ilan edilmedi");
    }

    // =========================================================================
    // D) Tek sayfaya sigmayan montaj -> bagli coklu pafta seti (kabul testi 5)
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(2400.0, 2);
        const std::uint64_t revBefore = f.doc.revision();
        MarkRegistry reg;
        DrawingOptions o;
        o.layout.fixedScaleDen = 5.0;   // kullanici olcegi sabitledi: kucultme yasak
        DrawingPreferences p;
        p.autoMax = SheetSize::A2;      // izinli ust sinir
        p.autoMultiSheetFallback = true;   // ESKI davranis: acikca istenirse bolme yapilir
        p.allowOverflow = false;           // bolme yolu eski kalite kapisiyla calisir
        DrawingSetResult r = buildDrawingSet(f.doc, reg, firstAssembly(f), o, p);
        std::size_t detailTotal = 0, maxDetails = 0;
        for (const DrawingSheet& s : r.document.sheets()) {
            std::size_t d = 0;
            for (const DrawingView& v : s.views) d += v.kind == ViewKind::Detail ? 1 : 0;
            detailTotal += d;
            maxDetails = std::max(maxDetails, d);
        }
        check(r.ok && !r.singleSheet && r.document.sheets().size() >= 2 &&
                  r.document.sheets().size() < detailTotal + 1,
              "D: tek pafta sigmadi -> bagli paftalar, detaylar TEK TEK dagitilmadi (en az sayfa)");
        check(maxDetails >= 2, "D: birden fazla detay ayni paftada toplandi (P2D sigdirma)");
        check(r.layoutValid && allSheetsPlaced(r.document), "D: TUM paftalarda yerlestim, bagimsiz olcumde cakisma 0");
        check(!r.issues.empty() && hasIssue(r.issues, "SINGLE_SHEET_NOT_VALID"),
              "D: tek pafta neden olmadi -> acik rapor (gizlenmedi)");

        const DrawingDocument& doc = r.document;
        const DrawingSheet& prim = doc.sheets().front();
        check(prim.role == SheetRole::Primary && doc.setInfo().primarySheetId == prim.id,
              "D: ana pafta tek ve setInfo ile tutarli");
        check(doc.setInfo().multiSheet && doc.setInfo().id.rfind("DS-", 0) == 0,
              "D: kalici DrawingSetId + multiSheet isareti");
        check(doc.setInfo().index.size() == doc.sheets().size() - 1, "D: ana pafta indeksi tum bagli paftalari listeler");
        bool numbering = true, parents = true, numbersUnique = true;
        std::set<std::string> nums;
        for (std::size_t i = 0; i < doc.sheets().size(); ++i) {
            const DrawingSheet& s = doc.sheets()[i];
            if (i > 0) {
                char want[64];
                std::snprintf(want, sizeof(want), "%s-%02d", prim.sheetNumber.c_str(), static_cast<int>(i));
                numbering = numbering && s.sheetNumber == want && s.titleBlock.drawingNumber == s.sheetNumber;
                parents = parents && s.parentSheetId == prim.id;
            }
            numbersUnique = numbersUnique && nums.insert(s.sheetNumber).second;
            if (s.titleBlock.revision != prim.titleBlock.revision || s.titleBlock.project != prim.titleBlock.project)
                numbering = false;
            if (s.layoutConfig.dimTextHeight < 3.0) numbering = false;
        }
        check(numbering, "D: numaralandirma (A100, A100-01..) + antet/revizyon tutarli, yazi kucultulmedi");
        check(numbersUnique && parents, "D: pafta numaralari tekil, her bagli pafta ana paftaya bagli");

        const DrawingView* mainView = prim.views.empty() ? nullptr : &prim.views.front();
        check(mainView != nullptr && prim.findView(mainView->id) != nullptr, "D: ana paftada ana gorunus var");
        bool resolved = true, bidirectional = true;
        for (const SheetReference& ref : prim.references) {
            const DrawingSheet* target = findSheet(doc, ref.targetSheetId);
            if (!target || ref.targetNumber != target->sheetNumber) {
                resolved = false;
                continue;
            }
            if (ref.targetViewId != 0 && !target->findView(ref.targetViewId)) resolved = false;
            bool back = false;
            for (const SheetReference& b : target->references)
                if (b.targetSheetId == prim.id) back = true;
            bidirectional = bidirectional && back;
        }
        check(!prim.references.empty() && resolved, "D: ana pafta -> hedef pafta/gorunus referanslari cozuldu");
        check(bidirectional, "D: bagli paftalar ana paftaya geri referans veriyor (cift yonlu)");
        bool detailRefs = true;
        for (std::size_t i = 1; i < doc.sheets().size(); ++i)
            if (doc.sheets()[i].role == SheetRole::DetailSheet) {
                bool hasFwd = false;
                for (const SheetReference& ref : prim.references)
                    if (ref.targetSheetId == doc.sheets()[i].id) hasFwd = true;
                detailRefs = detailRefs && hasFwd && !doc.sheets()[i].views.empty();
            }
        check(detailRefs, "D: her detay paftasi ana paftadan erisilebilir ve bir gorunus tasir");

        std::vector<std::string> vIssues;
        check(validateDrawingSet(doc, vIssues) && vIssues.empty(), "D: validateDrawingSet temiz");

        // Muhendislik degerleri: setteki olcu kumesi tek-pafta uretiminin olcu kumesiyle AYNI
        // (pafta bolme olcu EKLEMEZ/SILMEZ/DEGISTIRMEZ).
        DrawingOptions baseOpt = o;
        baseOpt.layout.minSheet = SheetSize::A3;
        baseOpt.layout.maxSheet = SheetSize::A2;
        baseOpt.autoLayout = true;
        MarkRegistry regBase;
        const BuildResult base = buildSheet(f.doc, regBase, firstAssembly(f), baseOpt);
        check(base.ok && setSignature(doc) == dimSignature(base.sheet) && dimSignature(base.sheet).size() > 40,
              "D: setteki olcu kumesi tek-pafta uretimiyle AYNI (eksik/fazla olcu yok)");
        bool measured = true;
        for (const DrawingSheet& s : doc.sheets())
            for (const Dimension2D& d : s.dimensions)
                if (d.role == DimensionRole::HolePitch || d.role == DimensionRole::HoleEdge ||
                    d.role == DimensionRole::PlateWidth || d.role == DimensionRole::PlateOffsetX)
                    measured = measured && nearTo(d.measured(), d.value, 1e-6);
        check(measured, "D: bolunen paftalarda imalat olculeri measured() == value");

        // Determinizm + JSON turu.
        MarkRegistry reg2;
        DrawingSetResult again = buildDrawingSet(f.doc, reg2, firstAssembly(f), o, p);
        check(again.document.serialize() == doc.serialize(), "D: tekrar uretim birebir ayni (deterministik)");
        const std::string json = doc.serialize();
        DrawingDocument back;
        std::string err;
        check(DrawingDocument::deserialize(json, back, err) && back.serialize() == json,
              "D: JSON save/load seti (referanslar dahil) birebir korur");
        std::vector<std::string> backIssues;
        check(validateDrawingSet(back, backIssues) && backIssues.empty(), "D: yuklenen set de dogrulanir");

        check(f.doc.revision() == revBefore, "D: coklu pafta uretimi Document'i DEGISTIRMEDI");
    }

    // =========================================================================
    // E) Bolme kapali + tasma modu kapali: tek pafta, kalite eksik acikca raporlanir
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(2400.0, 2);
        MarkRegistry reg;
        DrawingOptions o;
        o.layout.fixedScaleDen = 5.0;
        DrawingPreferences p;
        p.autoMax = SheetSize::A2;
        p.autoMultiSheetFallback = false;   // bolme yok
        p.allowOverflow = false;            // tasma modu kapali: eski kati kalite kapisi
        DrawingSetResult r = buildDrawingSet(f.doc, reg, firstAssembly(f), o, p);
        check(r.ok && r.singleSheet && r.document.sheets().size() == 1,
              "E: bolme+tasma kapali -> TEK pafta, coklu sayfa YOK, cizim yine uretilir");
        check(!r.layoutValid && !r.ready, "E: eksik yerlesim READY ilan EDILMEDI");
        check(hasIssue(r.issues, "SINGLE_SHEET_NOT_VALID"), "E: tek pafta nedeni acikca raporlandi");
        bool layoutItem = false;
        for (const ChecklistItem& c : r.document.sheets().front().fabrication.checklist)
            if (c.key == "LAYOUT" && c.state == ChecklistState::Unresolved) layoutItem = true;
        check(layoutItem, "E: kontrol listesinde LAYOUT unresolved (TASLAK)");
    }

    // =========================================================================
    // E2) Ayni montaj, VARSAYILAN (tasma modu acik): tek pafta cozulur, bolunmez
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(2400.0, 2);
        MarkRegistry reg;
        DrawingOptions o;
        o.layout.fixedScaleDen = 5.0;
        DrawingPreferences p;
        p.autoMax = SheetSize::A2;
        DrawingSetResult r = buildDrawingSet(f.doc, reg, firstAssembly(f), o, p);
        check(r.ok && r.singleSheet && r.document.sheets().size() == 1,
              "E2: tasma modu acik -> ayni montaj TEK paftada cozuldu (bolunmedi)");
        const DrawingSheet& s = r.document.sheets().front();
        check(s.layout.ok, "E2: tek pafta yerlesimi gecerli (tasma olsa da kirpilmadi)");
        if (s.layout.overflow) check(r.document.setInfo().multiSheet == false, "E2: tasma -> coklu pafta ACILMADI");
    }

    // =========================================================================
    // F) Coklu pafta da cozemezse: cozulmemis sorunlar acikca raporlanir
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(1800.0, 0);
        MarkRegistry reg;
        DrawingOptions o;
        o.layout.fixedScaleDen = 5.0;
        DrawingPreferences p;
        p.autoMax = SheetSize::A3;   // dar sinir: bazi paftalar sigmaz
        p.autoMultiSheetFallback = true;   // bolme acikca istendi
        p.allowOverflow = false;           // bolme yolu eski kalite kapisiyla calisir
        DrawingSetResult r = buildDrawingSet(f.doc, reg, firstAssembly(f), o, p);
        check(r.ok && r.document.sheets().size() > 1, "F: dar kagit sinirinda coklu pafta uretildi");
        check(!r.layoutValid && !r.ready, "F: yerlesmeyen pafta varsa set READY DEGIL");
        check(hasIssue(r.issues, "SHEET "), "F: cozulmemis pafta/sorun kodu acikca listelendi");
        bool unresolved = false;
        for (const DrawingSheet& s : r.document.sheets())
            if (!s.layout.ok || measureOverlaps(s).total() != 0) {
                for (const ChecklistItem& c : s.fabrication.checklist)
                    if (c.key == "LAYOUT" && c.state == ChecklistState::Unresolved) unresolved = true;
            }
        check(unresolved, "F: yerlesmeyen pafta TASLAK olarak isaretli");
    }

    // =========================================================================
    // G) Kullanici sabit kagit sectiyse sinirlar asilmaz
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(1800.0, 0);
        MarkRegistry reg;
        DrawingPreferences p;
        p.autoSheetSize = false;
        p.sheetSize = SheetSize::A2;
        DrawingSetResult r = buildDrawingSet(f.doc, reg, firstAssembly(f), {}, p);
        check(r.ok && r.singleSheet, "G: sabit A2 tercihi -> tek pafta");
        bool sizes = true;
        for (const DrawingSheet& s : r.document.sheets())
            sizes = sizes && s.format.size == SheetSize::A2 && s.layout.requestedSheet == SheetSize::A2;
        check(sizes, "G: secilen A2 siniri asilmadi (A1/A0 denemedi)");
        DrawingSetResult again = buildDrawingSet(f.doc, reg, firstAssembly(f), {}, p);
        check(again.document.serialize() == r.document.serialize(), "G: sabit kagit tercihi deterministik");
    }

    // =========================================================================
    // H) Kucuk kagit (A4) + bolme kapali: TEK pafta, hicbir gorunus/olcu DUSMEZ
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(1800.0, 0);
        MarkRegistry reg;
        DrawingPreferences p;
        p.autoSheetSize = false;
        p.sheetSize = SheetSize::A4;
        p.autoMultiSheetFallback = false;
        DrawingSetResult r = buildDrawingSet(f.doc, reg, firstAssembly(f), {}, p);
        check(r.ok && r.singleSheet && r.document.sheets().size() == 1,
              "H: A4 + bolme kapali -> tek pafta (kirpilmaz, bolunmez)");
        const DrawingSheet& s = r.document.sheets().front();
        check(!s.views.empty() && s.format.size == SheetSize::A4 && s.layout.requestedSheet == SheetSize::A4,
              "H: secilen kagit korunur, gorunusler silinmez");
        MarkRegistry regBase;
        const BuildResult base = buildSheet(f.doc, regBase, firstAssembly(f), {});
        check(base.ok && s.dimensions.size() == base.sheet.dimensions.size(),
              "H: tasma/tek-pafta modu olcu EKLEMEZ/SILMEZ");
        check(s.layout.ran && (s.layout.ok || !s.layout.issues.empty()),
              "H: yerlesim raporu tutarli (ok ise tasma da bildirilir, degilse issue dolu)");
    }

    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
