// ASSEMBLY CIZIMI ILERLEME BILDIRIMI — Qt'siz cekirdek dogrulamasi.
//
// Kural: ilerleme YALNIZ gercek asamalar icin bildirilir; sahte yuzde uretilmez ve
// bildirim cizim SONUCUNU (geometri/olcu/JSON) DEGISTIRMEZ.
//
// Dogrulananlar:
//   * callback dolu -> 4 asama, done 0..4 artan, ilk 0, son 4, etiketler bos degil,
//   * ayni girdi -> ayni asama dizisi (deterministik),
//   * callback'siz uretim ile callback'li uretim BIREBIR ayni JSON'u verir,
//   * tek pafta ve coklu pafta yollarinda da asama sayisi ayni (4).

#include "fab_fixture.hpp"
#include "model_maker/drawing_set.hpp"
#include "model_maker/progress_report.hpp"

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

static fx::Fixture makeAssembly(double len, int extraPlates) {
    fx::Fixture f = fx::make({}, len, true);
    for (int i = 0; i < extraPlates; ++i) {
        const double x = 600.0 + 400.0 * static_cast<double>(i);
        const EntityId p = fx::addFramedPlate(f, {x, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 300, 200, 10,
                                              "S355", "Kanat");
        fx::addBolts(f, p, {x - 100, 80, 30}, {0, 1, 0}, {1, 0, 0}, 3, 2, 100, 60, 22);
    }
    return f;
}

static std::vector<ProgressReport> collect(const fx::Fixture& f, EntityId subject, const DrawingPreferences& p) {
    std::vector<ProgressReport> events;
    MarkRegistry reg;
    (void)buildDrawingSet(f.doc, reg, subject, DrawingOptions{}, p,
                          [&events](const ProgressReport& report) { events.push_back(report); });
    return events;
}

static bool stagesMonotonic(const std::vector<ProgressReport>& events, int expectedTotal) {
    int previous = -1;
    for (const ProgressReport& e : events) {
        if (e.stage.empty()) return false;
        if (e.total != expectedTotal) return false;
        if (e.done < previous) return false;
        if (e.done < 0 || e.done > expectedTotal) return false;
        previous = e.done;
    }
    return true;
}

int main() {
    fx::Fixture single = fx::make({}, 4000.0, true);
    const EntityId singleSubject = single.beam;
    MarkRegistry reg;
    DrawingPreferences prefsSingle;
    prefsSingle.autoMultiSheetFallback = false;   // tek pafta yolu

    std::vector<ProgressReport> events;
    const DrawingSetResult withCallback =
        buildDrawingSet(single.doc, reg, singleSubject, DrawingOptions{}, prefsSingle,
                        [&events](const ProgressReport& report) { events.push_back(report); });

    check(withCallback.ok, "tek pafta: cizim uretildi");
    check(!events.empty(), "tek pafta: ilerleme bildirimleri geldi");
    check(stagesMonotonic(events, 4), "tek pafta: 4 asama, done artan ve 0..4 araliginda, etiketler dolu");
    check(events.front().done == 0, "tek pafta: ilk bildirim 0. asamada");
    check(events.back().done == 4, "tek pafta: son bildirim tum asamalar bitti (4)");

    // Determinizm: ayni girdi -> ayni asama dizisi
    const std::vector<ProgressReport> again = collect(single, singleSubject, prefsSingle);
    bool same = again.size() == events.size();
    for (std::size_t i = 0; same && i < again.size(); ++i) {
        same = again[i].stage == events[i].stage && again[i].done == events[i].done &&
               again[i].total == events[i].total;
    }
    check(same, "tek pafta: asama dizisi deterministik (tekrar uretim ayni)");

    // Sonuc bagimsizligi: callback'li ve callback'siz uretim birebir ayni JSON
    MarkRegistry regPlain;
    const DrawingSetResult withoutCallback =
        buildDrawingSet(single.doc, regPlain, singleSubject, DrawingOptions{}, prefsSingle);
    check(withoutCallback.ok && withoutCallback.document.serialize() == withCallback.document.serialize(),
          "ilerleme callback'i cizim ciktisini DEGISTIRMEZ (JSON birebir)");

    // Coklu pafta yolu: ayni 4 asama sozlesmesi
    fx::Fixture assembly = makeAssembly(2400.0, 3);
    const EntityId asmId = assembly.doc.assemblies().empty() ? 0 : assembly.doc.assemblies()[0].id;
    DrawingPreferences prefsMulti;
    prefsMulti.autoMax = SheetSize::A2;
    prefsMulti.autoMultiSheetFallback = true;
    std::vector<ProgressReport> multiEvents;
    MarkRegistry regMulti;
    const DrawingSetResult multi = buildDrawingSet(assembly.doc, regMulti, asmId, DrawingOptions{}, prefsMulti,
                                                  [&multiEvents](const ProgressReport& report) {
                                                      multiEvents.push_back(report);
                                                  });
    check(multi.ok, "assembly: cizim seti uretildi");
    check(!multiEvents.empty() && stagesMonotonic(multiEvents, 4),
          "assembly: 4 asama, done artan, etiketler dolu");
    check(multiEvents.back().done == 4, "assembly: son bildirimde ilerleme tamamlandi");

    // Hata yolu: konu cizilemezse ilerleme yine de tutarli (0..4, artan) kalir.
    std::vector<ProgressReport> badEvents;
    MarkRegistry regBad;
    const DrawingSetResult bad = buildDrawingSet(single.doc, regBad, 999999, DrawingOptions{}, prefsSingle,
                                                 [&badEvents](const ProgressReport& report) {
                                                     badEvents.push_back(report);
                                                 });
    const bool badMonotonic = [&]() {
        int previous = -1;
        for (const ProgressReport& e : badEvents) {
            if (e.total != 4 || e.done < previous || e.done < 0 || e.done > 4 || e.stage.empty()) return false;
            previous = e.done;
        }
        return true;
    }();
    check(!bad.ok, "gecersiz konu: cizim uretilmedi (sahte basari yok)");
    check(badMonotonic, "gecersiz konu: ilerleme bildirimleri yine gecerli (sahte yuzde yok)");

    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
