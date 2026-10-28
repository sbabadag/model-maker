// P2D: GORUNUS BASINA OLCEK + EN AZ BUYUTME. Kullanici sikayeti (2026-10-28):
// "cizimler bir A0 pafta; A1'e de sigar normalde, paftalari boluyor".
// Kural: uzun gorunus (kiriş, 5000 mm) ile kucuk gorunus (birlesim yuzu) AYNI paftada durabilmeli;
// kucuk gorunus gereksiz yere 1:2'ye kadar buyutulup kagidi doldurmamali; zorlanmis olcek korunmali.
#include "fab_fixture.hpp"
#include "model_maker/drawing_set.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mm;
using namespace mm::drawing;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Kiris Y ekseninde: FRONT kucuk (kiris eksenine bakar), TOP/SIDE 5000 mm uzun.
static DrawingSetResult buildRot(double len, SheetSize maxSheet, bool fixedFive, const char* tag) {
    fx::Fixture f = fx::make(fx::rotZ(90), len, true);
    MarkRegistry reg;
    const EntityId subj = f.doc.assemblies().empty() ? 0 : f.doc.assemblies()[0].id;
    DrawingPreferences prefs;
    prefs.autoSheetSize = true;
    prefs.autoFirst = SheetSize::A3;
    prefs.autoMax = maxSheet;
    DrawingOptions o;
    if (fixedFive) o.layout.fixedScaleDen = 5.0;
    DrawingSetResult r = buildDrawingSet(f.doc, reg, subj, o, prefs);
    std::printf("-- %s (len=%.0f, max=%s): pafta=%zu tek=%d ok=%d\n", tag, len, sheetSizeName(maxSheet),
                r.document.sheets().size(), (int)r.singleSheet, (int)r.ok);
    return r;
}

static const char* kindName(ViewKind k) {
    switch (k) {
    case ViewKind::Front: return "FRONT";
    case ViewKind::Top: return "TOP";
    case ViewKind::Side: return "SIDE";
    case ViewKind::Detail: return "DETAY";
    }
    return "?";
}

// Hizali projeksiyon gorunusleri (FRONT/TOP/SIDE) TEK olcekte olmali; yalniz DETAY ayri olcek alabilir.
static bool mainViewsShareScale(const DrawingSheet& s, double* den) {
    bool any = false;
    double first = 0.0;
    for (const DrawingView& v : s.views) {
        if (v.kind == ViewKind::Detail || v.scale <= 0.0) continue;
        const double d = 1.0 / v.scale;
        if (!any) { first = d; any = true; continue; }
        if (std::fabs(d - first) > 1e-6) return false;
    }
    if (den) *den = first;
    return any;
}

int main() {
    // A: 5000 mm kiriş + levhalar, A3..A2 aralığı (kullanıcının ayarı) → TEK pafta.
    {
        const DrawingSetResult r = buildRot(5000.0, SheetSize::A2, false, "A kisa kiris");
        check(r.ok && r.singleSheet && r.document.sheets().size() == 1, "A: 5000 mm kiriş A2'ye TEK paftada sigar");
        check(r.layoutValid && r.issues.empty(), "A: tek pafta cakismasiz (issues bos)");
        double den = 0.0;
        check(mainViewsShareScale(r.document.sheets().front(), &den),
              "A: FRONT/TOP/SIDE AYNI olcekte (hizali projeksiyon)");
        std::printf("   ana olcek 1:%.0f, pafta olcegi 1:%.0f\n", den, 1.0 / r.document.sheets().front().scale);
        for (const DrawingView& v : r.document.sheets().front().views)
            std::printf("   %-6s 1:%.0f kagit=%.0fx%.0f\n", kindName(v.kind), 1.0 / v.scale,
                        v.bounds.width() * v.scale, v.bounds.height() * v.scale);
    }
    // B: 12000 mm canli (uzun) + ayni kucuk gorunus → hala TEK pafta, olcek buyutme makul.
    {
        const DrawingSetResult r = buildRot(12000.0, SheetSize::A2, false, "B uzun kiris");
        check(r.ok && r.singleSheet && r.document.sheets().size() == 1, "B: 12000 mm kiriş de A2'ye TEK paftada");
        const DrawingSheet& s = r.document.sheets().front();
        double worst = 1.0, biggest = 0.0;
        for (const DrawingView& v : s.views) {
            if (v.kind == ViewKind::Detail) continue;
            biggest = std::max(biggest, v.bounds.width() * v.scale);
            biggest = std::max(biggest, v.bounds.height() * v.scale);
            worst = std::max(worst, v.scale / s.scale);   // gorunus olcegi / pafta olcegi
        }
        check(worst <= 1.0 + 1e-9, "B: ana gorunusler pafta olceginden buyuk degil (tek olcek)");
        double den = 0.0;
        check(mainViewsShareScale(s, &den), "B: FRONT/TOP/SIDE AYNI olcekte (tek olcek)");
        const Bounds2 area = s.drawingArea();
        check(biggest <= area.width() + 1e-6 && biggest <= area.height() + 1e-6,
              "B: hicbir gorunus kagit alanindan buyuk degil");
    }
    // C: zorlanmis olcek (fixedScaleDen) kullanici kararidir → gorunus olcekleri DEGISMEZ.
    {
        const DrawingSetResult r = buildRot(5000.0, SheetSize::A1, true, "C zorlanmis 1:5");
        bool allFive = true, anyMain = false;
        for (const DrawingView& v : r.document.sheets().front().views) {
            if (v.kind == ViewKind::Detail) continue;
            anyMain = true;
            if (std::fabs(1.0 / v.scale - 5.0) > 1e-6) allFive = false;
        }
        check(anyMain && allFive, "C: zorlanmis 1:5 korunur (gorunus basina buyutme yapilmaz)");
    }
    std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
