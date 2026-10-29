// P2D — AKILLI PAFTA YERLESIMI / SIGDIRMA (OCCT'siz, Qt'siz).
//
// Kapsam (kabul):
//   * Sabit kagit (A2) secildiyse sinirlar asilmaz; otomatik modda A3 -> A0 eskalasyonu raporlanir.
//   * Detay gorunusleri TEK TEK paftalara dagitilmaz: sigdigi kadar ayni paftada toplanir (en az sayfa).
//   * Coklu pafta setinde hicbir gorunus/olcu kaybolmaz, cakisma 0, numaralandirma/referans gecerli.
//   * Durum metni secilen kagit/olcek/sebebi yazar; set tekrar uretimde birebir ayni (deterministik).
//   * Model (Document) DEGISMEZ.

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
#include "legacy_drawing_fixture.hpp"

static int failures = 0;
static int checks = 0;
static void check(bool ok, const std::string& label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}
static bool nearTo(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }
static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

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
static EntityId assemblyOf(const fx::Fixture& f) { return f.doc.assemblies().empty() ? 0 : f.doc.assemblies()[0].id; }

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
static bool allSheetsPlaced(const DrawingDocument& doc) {
    bool ok = true;
    for (const DrawingSheet& s : doc.sheets()) ok = ok && s.layout.ok && measureOverlaps(s).total() == 0;
    return ok;
}
static int detailCount(const DrawingSheet& s) {
    int n = 0;
    for (const DrawingView& v : s.views) n += v.kind == ViewKind::Detail ? 1 : 0;
    return n;
}

int main() {
    // =========================================================================
    // A) SABIT A2: kagit siniri asilmaz, detaylar ayni paftada toplanir (en az sayfa)
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(2400.0, 2);
        const std::uint64_t rev0 = f.doc.revision();
        MarkRegistry reg;
        DrawingOptions o;
        o.layout.fixedScaleDen = 5.0;   // kullanici olcek sabitledi: kucultme yasak
        DrawingPreferences p;
        p.autoSheetSize = false;
        p.sheetSize = SheetSize::A2;
        // 2026-10-29: pafta BOLME artik varsayilan degil; bu test bolme makinesini dogrular.
        p.autoMultiSheetFallback = true;
        p.allowOverflow = false;
        const DrawingSetResult r = legacyBuildDrawingSet(f.doc, reg, assemblyOf(f), o, p);
        check(r.ok && r.document.sheets().size() >= 2, "A: sigmayan montaj -> bagli coklu pafta seti");
        bool fixedPaper = true, requested = true;
        for (const DrawingSheet& s : r.document.sheets()) {
            fixedPaper = fixedPaper && s.format.size == SheetSize::A2;
            requested = requested && s.layout.requestedSheet == SheetSize::A2 && s.layout.finalSheet == SheetSize::A2;
        }
        check(fixedPaper, "A: secilen A2 korunur (A1/A0'a sessiz eskalasyon YOK)");
        check(requested, "A: her pafta A2 istendi/A2 yerlesildi (rapor tutarli)");
        check(r.layoutValid && allSheetsPlaced(r.document), "A: tum paftalar gecerli, bagimsiz olcumde cakisma 0");

        int maxDetails = 0;
        for (const DrawingSheet& s : r.document.sheets()) maxDetails = std::max(maxDetails, detailCount(s));
        const int totalDetails = [] (const DrawingDocument& d) {
            int n = 0;
            for (const DrawingSheet& s : d.sheets()) n += detailCount(s);
            return n;
        }(r.document);
        check(totalDetails >= 4, "A: detay gorunusleri korunmus (>=4)");
        check(maxDetails >= 2, "A: birden fazla detay AYNI paftada (tek tek pafta acilmadi)");
        check(r.document.sheets().size() <= static_cast<std::size_t>(totalDetails),
              "A: pafta sayisi detay sayisindan fazla DEGIL (en az sayfa politikasi)");

        std::vector<std::string> issues;
        check(validateDrawingSet(r.document, issues) && issues.empty(), "A: set dogrulandi (numara/referans/antet)");
        check(r.plan.size() == r.document.sheets().size(), "A: her pafta icin plan kaydi var");
        bool planOk = true;
        for (const SheetPlanEntry& e : r.plan)
            planOk = planOk && e.size == SheetSize::A2 && !e.scale.empty() && !e.reason.empty() && e.views > 0;
        check(planOk, "A: plan kayitlari kagit + olcek + sebep + gorunus sayisi tasir");

        // Durum metni: secilen kagit/olcek ve coklu pafta sebebi okunur.
        const std::string st = statusText(r);
        check(contains(st, "pafta") && contains(st, "A2") && contains(st, "1:5") && contains(st, "bagli coklu pafta"),
              "A: durum metni kagit/olcek/coklu pafta bilgisini yazar");
        check(contains(st, "en az sayfa") || contains(st, "ek gorunus"),
              "A: durum metni yerlesim SEBEBINI yazar (gizlemez)");

        // Olcu kumesi: paftalara bolmek olcu eklemez/silmez.
        MarkRegistry regBase;
        DrawingOptions baseOpt = o;
        baseOpt.layout.minSheet = SheetSize::A2;
        baseOpt.layout.maxSheet = SheetSize::A2;
        baseOpt.autoLayout = true;
        const BuildResult base = legacyBuildSheet(f.doc, regBase, assemblyOf(f), baseOpt);
        check(base.ok && setSignature(r.document) == dimSignature(base.sheet) && dimSignature(base.sheet).size() > 40,
              "A: setteki olcu kumesi tek-pafta uretimiyle AYNI (eksik/fazla olcu yok)");
        bool measured = true;
        for (const DrawingSheet& s : r.document.sheets())
            for (const Dimension2D& d : s.dimensions) measured = measured && nearTo(d.measured(), d.value, 1e-6);
        check(measured, "A: bolunen paftalarda measured() == value");

        // Determinizm + JSON.
        MarkRegistry reg2;
        const DrawingSetResult again = legacyBuildDrawingSet(f.doc, reg2, assemblyOf(f), o, p);
        check(again.document.serialize() == r.document.serialize(), "A: tekrar uretim birebir ayni (deterministik)");
        bool planSame = again.plan.size() == r.plan.size();
        for (std::size_t i = 0; planSame && i < r.plan.size(); ++i)
            planSame = again.plan[i].number == r.plan[i].number && again.plan[i].size == r.plan[i].size &&
                       again.plan[i].scale == r.plan[i].scale && again.plan[i].reason == r.plan[i].reason;
        check(planSame, "A: plan (kagit/olcek/sebep) tekrar uretimde ayni");
        DrawingDocument back;
        std::string err;
        check(DrawingDocument::deserialize(r.document.serialize(), back, err) && back.serialize() == r.document.serialize(),
              "A: JSON round-trip birebir (deterministik cikti)");
        check(f.doc.revision() == rev0, "A: coklu pafta uretimi Document'i DEGISTIRMEDI");
    }

    // =========================================================================
    // B) SABIT A2: butun gorunusler (ana + detaylar) TEK paftada sigiyorsa tek pafta
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(1800.0, 2);
        MarkRegistry reg;
        DrawingOptions o;
        o.layout.fixedScaleDen = 5.0;
        DrawingPreferences p;
        p.autoSheetSize = false;
        p.sheetSize = SheetSize::A2;
        const DrawingSetResult r = legacyBuildDrawingSet(f.doc, reg, assemblyOf(f), o, p);
        check(r.ok && r.singleSheet && r.document.sheets().size() == 1,
              "B: ana + detay gorunusleri ayni paftada sigdiginda TEK okunakli pafta");
        const DrawingSheet& s = r.document.sheets().front();
        check(detailCount(s) >= 4 && s.views.size() >= 7, "B: tek paftada ana gorunusler + tum detaylar");
        check(s.layout.ok && measureOverlaps(s).total() == 0, "B: tek paftada cakisma 0");
        check(contains(statusText(r), "tek pafta"), "B: durum metni tek pafta oldugunu yazar");
        check(s.layout.requestedSheet == SheetSize::A2 && s.layout.finalSheet == SheetSize::A2,
              "B: sabit kagit siniri korunur");
    }

    // =========================================================================
    // C) OTOMATIK kagit: A3 yetmez -> A2 (rapor: eskalasyon), tek pafta
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(2400.0, 2);
        MarkRegistry reg;
        const DrawingSetResult r = legacyBuildDrawingSet(f.doc, reg, assemblyOf(f), {}, DrawingPreferences{});
        check(r.ok && r.singleSheet && r.document.sheets().size() == 1, "C: otomatik modda tek pafta");
        const DrawingSheet& s = r.document.sheets().front();
        check(s.format.size >= SheetSize::A2 && s.layout.requestedSheet == SheetSize::A3 &&
                  s.layout.finalSheet == s.format.size,
              "C: A3 istenip yetmeyince A2'ye buyutuldu (rapor tutarli)");
        check(s.layout.scaleDenominator > 0.0 && !s.titleBlock.scaleText.empty(), "C: olcek yazildi");
        check(contains(r.plan.front().reason, "buyutuldu") && contains(r.plan.front().reason, "A3") &&
                  contains(r.plan.front().reason, "A2"),
              "C: plan sebebi kagit buyutmeyi acikca yazar");
        check(contains(statusText(r), "A2") && contains(statusText(r), "1:"),
              "C: durum metni nihai kagit ve olcegi yazar");
    }

    // =========================================================================
    // D) OTOMATIK: A2 yetmez -> A1 (ust sinir A0), tek pafta + sebep
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(2400.0, 2);
        MarkRegistry reg;
        DrawingOptions o;
        o.layout.fixedScaleDen = 4.0;   // 2400/4 = 600 mm: A2'ye sigmaz, A1'e sigar
        const DrawingSetResult r = legacyBuildDrawingSet(f.doc, reg, assemblyOf(f), o, DrawingPreferences{});
        check(r.ok && r.singleSheet, "D: sabit 1:4 olcekte tek pafta");
        const DrawingSheet& s = r.document.sheets().front();
        check(s.format.size == SheetSize::A1 && s.layout.requestedSheet == SheetSize::A3,
              "D: A3 -> (A2 yetmez) -> A1 eskalasyonu");
        check(contains(r.plan.front().reason, "A1"), "D: plan sebebi nihai kagidi yazar");
        check(s.layout.ok && measureOverlaps(s).total() == 0, "D: A1'de cakisma 0, yazi kucultulmedi");
        check(s.layoutConfig.dimTextHeight >= 3.0 && s.layoutConfig.noteTextHeight >= 3.5,
              "D: okunabilir minimum yazi yuksekligi korunur");
    }

    // =========================================================================
    // E) SABIT A3 dar kagit: sigmayanlar acikca NO_FIT, detaylar yine gruplanir, sahte READY yok
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(1200.0, 3);
        MarkRegistry reg;
        DrawingOptions o;
        o.layout.fixedScaleDen = 5.0;
        DrawingPreferences p;
        p.autoSheetSize = false;
        p.sheetSize = SheetSize::A3;
        p.autoMultiSheetFallback = true;   // bolme makinesi testi (2026-10-29: varsayilan kapali)
        p.allowOverflow = false;
        const DrawingSetResult r = legacyBuildDrawingSet(f.doc, reg, assemblyOf(f), o, p);
        check(r.ok && r.document.sheets().size() > 1, "E: dar kagit -> coklu pafta");
        int maxDetails = 0;
        bool anyNoFit = false;
        for (const DrawingSheet& s : r.document.sheets()) {
            maxDetails = std::max(maxDetails, detailCount(s));
            if (!s.layout.ok) anyNoFit = true;
        }
        check(maxDetails >= 2, "E: dar kagitta da detaylar ayni paftada toplanir");
        check(anyNoFit && !r.layoutValid && !r.ready, "E: sigmayan pafta varsa set READY DEGIL (NO_FIT)");
        bool honest = false;
        for (const DrawingSheet& s : r.document.sheets())
            if (!s.layout.ok)
                for (const ChecklistItem& c : s.fabrication.checklist)
                    if (c.key == "LAYOUT" && c.state == ChecklistState::Unresolved) honest = true;
        check(honest, "E: yerlesmeyen pafta kontrol listesinde TASLAK");
        bool allA3 = true;
        for (const DrawingSheet& s : r.document.sheets()) allA3 = allA3 && s.format.size == SheetSize::A3;
        check(allA3, "E: A3 siniri asilmadi");
    }

    // =========================================================================
    // F) Hicbir gorunus kaybolmaz / tekrarlanmaz (coklu pafta seti)
    // =========================================================================
    {
        fx::Fixture f = makeAssembly(2400.0, 2);
        MarkRegistry reg;
        DrawingOptions o;
        o.layout.fixedScaleDen = 5.0;
        DrawingPreferences p;
        p.autoSheetSize = false;
        p.sheetSize = SheetSize::A2;
        p.autoMultiSheetFallback = true;   // bolme makinesi testi
        p.allowOverflow = false;
        const DrawingSetResult r = legacyBuildDrawingSet(f.doc, reg, assemblyOf(f), o, p);
        MarkRegistry regBase;
        DrawingOptions baseOpt = o;
        baseOpt.layout.minSheet = SheetSize::A2;
        baseOpt.layout.maxSheet = SheetSize::A2;
        baseOpt.autoLayout = true;
        const BuildResult base = legacyBuildSheet(f.doc, regBase, assemblyOf(f), baseOpt);
        std::multiset<int> setKinds;
        for (const DrawingSheet& s : r.document.sheets())
            for (const DrawingView& v : s.views) setKinds.insert(static_cast<int>(v.kind));
        std::multiset<int> baseKinds;
        for (const DrawingView& v : base.sheet.views) baseKinds.insert(static_cast<int>(v.kind));
        check(!baseKinds.empty() && setKinds == baseKinds, "F: gorunusler kaybolmadi/tekrarlanmadi (tur bazinda birebir)");
        bool refsOk = true;
        for (std::size_t i = 1; i < r.document.sheets().size(); ++i) {
            bool back = false;
            for (const SheetReference& ref : r.document.sheets()[i].references)
                if (ref.targetSheetId == r.document.sheets().front().id) back = true;
            refsOk = refsOk && back;
        }
        check(refsOk, "F: her bagli pafta ana paftaya geri referans veriyor");
    }

    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
