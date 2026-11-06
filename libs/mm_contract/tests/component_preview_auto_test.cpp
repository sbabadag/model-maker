// MM-6b — OTOMATIK ONIZLEME TANIMI (duyarlilik analizi) testi (OCC GEREKTIRMEZ).
//
// autoPreviewDefinition'in end plate icin:
//   1) >= 10 olcu tanimi urettigini (item 6),
//   2) anahtar parametreler icin EKSEN + ANCHOR eslestigini (kalinlik, tasma,
//      bulon araliklari, bulon kenar),
//   3) determinizmi (aynı girdi -> aynı tanim),
//   4) her uretilen olcunun anchor cozucuyle GECERLI oldugunu (ok=true),
//   5) hicbir plan farki uretmeyen Length parametresinin "etkisiz" raporlandigini.
#include "model_maker/component_codec.hpp"
#include "model_maker/component_dialog.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_preview.hpp"
#include "model_maker/component_preview_auto.hpp"
#include "model_maker/geometry.hpp"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static ComponentInput makeInput() {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = {0, 0, 1};
    in.main.secWidth = 300;
    in.main.secHeight = 300;
    in.main.secWebThickness = 11;
    in.main.secFlangeThickness = 19;

    MemberGeometry b;
    b.id = 2;
    b.axisFrom = {0, 0, 3000};
    b.axisTo = {2000, 0, 3000};
    b.refNormal = {0, 0, 1};
    b.secWidth = 200;
    b.secHeight = 400;
    b.secWebThickness = 8.6;
    b.secFlangeThickness = 13.5;
    in.secondary.push_back(b);
    return in;
}

static bool hasDim(const PreviewDefinition& def, const std::wstring& param,
                   const std::wstring& from, const std::wstring& to, wchar_t axis) {
    for (const auto& d : def.dimensions)
        if (d.param == param && d.from == from && d.to == to && d.axis == axis) return true;
    return false;
}

static bool hasDimParamAxis(const PreviewDefinition& def, const std::wstring& param,
                            wchar_t axis) {
    for (const auto& d : def.dimensions)
        if (d.param == param && d.axis == axis) return true;
    return false;
}

static bool inList(const std::vector<std::wstring>& v, const std::wstring& s) {
    for (const auto& x : v)
        if (x == s) return true;
    return false;
}

static bool hasLabel(const PreviewDefinition& def, const std::wstring& param) {
    for (const auto& l : def.labels)
        if (l.param == param) return true;
    return false;
}

static std::wstring reasonFor(const AutoPreviewResult& r, const std::wstring& name) {
    for (std::size_t i = 0; i < r.ineffective.size(); ++i)
        if (r.ineffective[i] == name)
            return i < r.ineffectiveReasons.size() ? r.ineffectiveReasons[i] : std::wstring{};
    return std::wstring{};
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const std::vector<CompParamSpec> specs = endPlateDialogSpecs();
    const std::function<GenerationPlan(const ComponentInput&)> gen = [](const ComponentInput& in) {
        return generateEndPlate(in);
    };
    const ComponentInput base = makeInput();

    const AutoPreviewResult r = autoPreviewDefinition(gen, base, specs);
    check(r.ok, "0 otomatik tanim uretildi (ok)");
    if (!r.ok) {
        std::printf("   hata: %ls\n", r.error.c_str());
        return 1;
    }

    // 1) En az 10 olcu.
    check(r.definition.dimensions.size() >= 10u, "1 >= 10 olcu tanimi uretildi");
    std::printf("   uretilen olcu sayisi: %zu\n", r.definition.dimensions.size());

    // 2) Anahtar parametreler icin eksen + anchor eslesmesi.
    check(hasDim(r.definition, L"plateThickness", L"end_plate.face_near",
                 L"end_plate.face_far", L'w'),
          "2 kalinlik: face_near -> face_far (w)");
    check(hasDimParamAxis(r.definition, L"plateExtra", L'u'), "2 plateExtra: u ekseni");
    check(hasDim(r.definition, L"boltSpacingX", L"beam_bolts.col_first",
                 L"beam_bolts.col_last", L'u'),
          "2 boltSpacingX: col_first -> col_last (u)");
    check(hasDim(r.definition, L"boltSpacingY", L"beam_bolts.row_first",
                 L"beam_bolts.row_last", L'v'),
          "2 boltSpacingY: row_first -> row_last (v)");
    check(hasDimParamAxis(r.definition, L"boltEdge", L'u') ||
              hasDimParamAxis(r.definition, L"boltEdge", L'v'),
          "2 boltEdge: kenar olcusu (u|v)");

    // 3) Determinizm: aynı girdi -> aynı tanim (param/from/to/axis birebir).
    {
        const AutoPreviewResult r2 = autoPreviewDefinition(gen, base, specs);
        bool same = r2.ok && r2.definition.dimensions.size() == r.definition.dimensions.size();
        if (same) {
            for (std::size_t i = 0; i < r.definition.dimensions.size(); ++i) {
                const auto& a = r.definition.dimensions[i];
                const auto& b = r2.definition.dimensions[i];
                if (!(a.param == b.param && a.from == b.from && a.to == b.to &&
                      a.axis == b.axis)) {
                    same = false;
                    break;
                }
            }
        }
        check(same, "3 determinizm: ayni girdi -> ayni tanim");
    }

    // 4) Her olcu anchor cozucuyle GECERLI ve deger >= 0.
    {
        const GenerationPlan plan = generateEndPlate(base);
        bool allValid = true;
        for (const auto& d : r.definition.dimensions) {
            const PreviewMeasurement m = measurePreviewDimension(plan, base, d);
            if (!m.ok || m.value < -1e-6) {
                allValid = false;
                std::printf("   GECERSIZ olcu: %ls (%ls -> %ls, %lc) = %f\n",
                            d.param.c_str(), d.from.c_str(), d.to.c_str(),
                            static_cast<wchar_t>(d.axis), m.value);
            }
        }
        check(allValid, "4 tum olcu anchor cozucuyle gecerli + deger >= 0");
    }

    // 5) MM-6b: bulon/delik capi ETKILI (etkisiz DEGIL); kenar mesafeleri
    //    (sentinelli "tanimsiz" varsayilan) bu modda ETKISIZ + sebep.
    {
        std::printf("   etkisiz parametreler: %zu\n", r.ineffective.size());
        for (std::size_t i = 0; i < r.ineffective.size(); ++i)
            std::printf("     - %ls  [%ls]\n", r.ineffective[i].c_str(),
                        i < r.ineffectiveReasons.size() ? r.ineffectiveReasons[i].c_str() : L"");
        // Cap parametreleri etkisiz DEGIL (plan farki uretir: bulon grubu capi).
        check(!inList(r.ineffective, L"boltDiameter"), "5 boltDiameter etkisiz DEGIL (etkili)");
        check(!inList(r.ineffective, L"holeDiameter"), "5 holeDiameter etkisiz DEGIL (etkili)");
        // Cap parametreleri bulon grubunda ETIKET uretir (olcu degil).
        check(hasLabel(r.definition, L"boltDiameter"), "5 boltDiameter etiketi uretildi");
        check(hasLabel(r.definition, L"holeDiameter"), "5 holeDiameter etiketi uretildi");
        // Kenar mesafeleri sentinelli varsayilan -> bu modda etkisiz + sebep.
        check(inList(r.ineffective, L"upperEdgeDistance"), "5 upperEdgeDistance etkisiz");
        check(inList(r.ineffective, L"lowerEdgeDistance"), "5 lowerEdgeDistance etkisiz");
        const std::wstring ur = reasonFor(r, L"upperEdgeDistance");
        check(!ur.empty() && ur.find(L"tanimsiz") != std::wstring::npos,
              "5 upperEdgeDistance sebebi 'tanimsiz' iceriyor");
    }

    // 5b) MM-6b mod bagimliligi: sentinelli kenar mesafesi TANIMLI (acik) verilince
    //     etkili olur — etkisiz listesinden duser (mod degisince normal).
    {
        ComponentInput defined = makeInput();
        defined.params.push_back(CompParam{L"upperEdgeDistance", CompParamValue{-30.0}});
        defined.params.push_back(CompParam{L"lowerEdgeDistance", CompParamValue{-30.0}});
        const AutoPreviewResult rd = autoPreviewDefinition(gen, defined, specs);
        check(rd.ok && !inList(rd.ineffective, L"upperEdgeDistance"),
              "5b tanimli upperEdgeDistance etkili (etkisiz DEGIL)");
        check(rd.ok && !inList(rd.ineffective, L"lowerEdgeDistance"),
              "5b tanimli lowerEdgeDistance etkili (etkisiz DEGIL)");
    }

    // 6) Onbellek anahtari deterministik.
    check(autoPreviewCacheKey(L"beam_to_column_end_plate", specs) ==
              autoPreviewCacheKey(L"beam_to_column_end_plate", specs),
          "6 onbellek anahtari deterministik");

    std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
