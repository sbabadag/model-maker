// TEKLA 146 STIFFENERS sekmesi — uretim plani testi (OCC'siz, Linux'ta kosar).
//
// Karsi-govde berkitmesi: konum (govdenin tab'a gore KARSI tarafi), olculer (gap 1 =
// flans, gap 2 = govde, t/b/h, creation tipleri), yonelim, pah, parca ozellikleri,
// Case A (kiris flans yuzune) / Case B (kiris govde normaline).

#include "model_maker/component_dialog.hpp"
#include "model_maker/component_engine.hpp"

#include <cmath>
#include <cstdio>
#include <optional>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; }

// HEB300 kolon (tw 11, tf 19) + IPE200 kiris (flans 8.5); kolon refNormal verilir.
static GenerationPlan gen(const Vec3& colRef, const std::vector<CompParam>& params) {
    ComponentRegistry reg;
    registerBuiltInComponents(reg);
    ComponentInput in;
    in.main.id = EntityId{20};
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = colRef;
    in.main.secWidth = 300.0;
    in.main.secHeight = 300.0;
    in.main.secWebThickness = 11.0;
    in.main.secFlangeThickness = 19.0;
    MemberGeometry beam;
    beam.id = EntityId{21};
    beam.axisFrom = {0, 0, 3000};
    beam.axisTo = {0, 4000, 3000};
    beam.refNormal = {0, 0, 1};
    beam.secWidth = 100.0;
    beam.secHeight = 200.0;
    beam.secWebThickness = 5.6;
    beam.secFlangeThickness = 8.5;
    in.secondary.push_back(beam);
    in.params = params;
    return reg.generate(L"tekla_shear_plate_146", in);
}
static CompParam S(const wchar_t* n, const wchar_t* v) {
    return CompParam{n, CompParamValue{std::wstring(v)}};
}
static CompParam D(const wchar_t* n, double v) { return CompParam{n, CompParamValue{v}}; }
static CompParam I(const wchar_t* n, int v) { return CompParam{n, CompParamValue{v}}; }

// DEGER dondurur (gecici GenerationPlan'a isaret eden sarkik gosterici olmasin).
static std::optional<PlateSpec> stiff(const GenerationPlan& p) {
    for (std::size_t i = 0; i < p.plateKeys.size() && i < p.plates.size(); ++i)
        if (p.plateKeys[i] == "stiffener") return p.plates[i];
    return std::nullopt;
}

int main() {
    const Vec3 refB{1, 0, 0};  // Case B: kiris govde normaline gelir
    const Vec3 refA{0, 1, 0};  // Case A: kiris kolon flans yuzune gelir

    // --- creation tipleri ----------------------------------------------------------
    check(!stiff(gen(refB, {})), "Default -> berkitme yok");
    check(!stiff(gen(refB, {S(L"stiffenerCreation", L"No stiffeners")})), "No stiffeners -> yok");
    {
        const GenerationPlan p = gen(refB, {S(L"stiffenerCreation", L"Full")});
        const auto s = stiff(p);
        check(p.ok && s.has_value(), "Full uretildi");
        if (s) {
            // b = 150-5.5-10, h = 200-17, t = tab (10)
            check(approx(s->width, 134.5) && approx(s->height, 183.0) && approx(s->thickness, 10.0),
                  "Full: b x h x t = 134.5 x 183 x 10");
            // tab duzleminde (x=-10), govdenin karsi tarafinda (y<0), kiris kotunda
            check(approx(s->center.x, -10.0) && approx(s->center.y, -82.75) && approx(s->center.z, 3000.0),
                  "Case B: merkez (-10, -82.75, 3000)");
            check(approx(s->axis.x, -1.0) && approx(s->u.y, -1.0), "Case B: normal = tab normali, u = disari");
        }
    }
    {
        const auto s = stiff(gen(refA, {S(L"stiffenerCreation", L"Full")}));
        check(s.has_value(), "Case A uretildi");
        if (s) {
            check(approx(s->width, 262.0) && approx(s->height, 183.0), "Case A: 262 x 183 (govdeye paralel)");
            // tab x<0 tarafinda, berkitme govdenin x>0 yaninda: x = 5.5 + 10 + 10/2
            check(approx(s->center.x, 20.5) && approx(s->center.y, 0.0), "Case A: merkez x=20.5 (karsi yan)");
        }
    }
    {
        const auto s = stiff(gen(refB, {S(L"stiffenerCreation", L"Determined by shear tab")}));
        check(s && approx(s->height, 240.0), "Determined by shear tab: h = tab (240)");
        const auto sp = stiff(gen(refB, {S(L"stiffenerCreation", L"Partial"), D(L"plateHeight", 120.0),
                                               I(L"boltCount", 2)}));
        check(sp && approx(sp->height, 151.5) && approx(sp->center.z, 3000.0 + 15.75),
              "Partial: ust Full, alt tab alti (h=151.5, z kaymasi 15.75)");
    }

    // --- gap, olculer -----------------------------------------------------------------
    {
        const auto s = stiff(gen(refB, {S(L"stiffenerCreation", L"Full"), D(L"stiffenerGapFlange", 5.0),
                                              D(L"stiffenerGapWeb", 4.0)}));
        check(s && approx(s->width, 150.0 - 5.5 - 4.0) && approx(s->height, 183.0 - 10.0),
              "gap1 (flans) dusey, gap2 (govde) disari uzunlugu");
        const auto d = stiff(gen(refB, {S(L"stiffenerCreation", L"Full"), D(L"stiffenerT", 14.0),
                                              D(L"stiffenerW", 100.0), D(L"stiffenerH", 120.0)}));
        check(d && approx(d->thickness, 14.0) && approx(d->width, 100.0) && approx(d->height, 120.0),
              "t/b/h acik girilince");
    }

    // --- yonelim ------------------------------------------------------------------------
    {
        const auto h = stiff(gen(refB, {S(L"stiffenerCreation", L"Full"),
                                              S(L"stiffenerOrientation", L"Perpendicular to main part")}));
        check(h && approx(h->axis.z, 1.0) && approx(h->width, 134.5) && approx(h->height, 262.0),
              "Perpendicular: yatay levha (normal=Z), 134.5 x 262");
        const auto par = stiff(gen(refB, {S(L"stiffenerCreation", L"Full"),
                                                S(L"stiffenerOrientation", L"Parallel to secondary part")}));
        check(par && approx(par->axis.x, -1.0), "Parallel to secondary part: dusey tab duzlemi");
    }

    // --- pah ------------------------------------------------------------------------------
    {
        const GenerationPlan p = gen(refB, {S(L"stiffenerCreation", L"Full"),
                                            S(L"stiffenerChamferType", L"Convex arc chamfer"),
                                            D(L"stiffenerChamferH", 20.0), D(L"stiffenerChamferV", 15.0)});
        const auto s = stiff(p);
        check(s && s->chamfers.size() == 2 && s->chamfers[0].corner == 0 && s->chamfers[1].corner == 3 &&
                  s->chamfers[0].type == 2 && approx(s->chamfers[0].h, 20.0) && approx(s->chamfers[0].v, 15.0),
              "pah: govde tarafi 2 kose, konveks, 20x15");
        const auto none = stiff(gen(refB, {S(L"stiffenerCreation", L"Full"),
                                                 S(L"stiffenerChamferType", L"No chamfer"),
                                                 D(L"stiffenerChamferH", 20.0), D(L"stiffenerChamferV", 15.0)}));
        check(none && none->chamfers.empty(), "No chamfer -> pah yok");
        const auto zero = stiff(gen(refB, {S(L"stiffenerCreation", L"Full"), D(L"stiffenerChamferH", 20.0)}));
        check(zero && zero->chamfers.empty(), "bacaklardan biri 0 -> pah yok");
        const auto a = stiff(gen(refA, {S(L"stiffenerCreation", L"Full"),
                                              D(L"stiffenerChamferH", 10.0), D(L"stiffenerChamferV", 10.0)}));
        check(a && a->chamfers.size() == 4 && a->chamfers[0].type == 1, "Case A: 4 kose, varsayilan tip = dogru pah");
    }

    // --- parca ozellikleri ---------------------------------------------------------------
    {
        const auto s = stiff(gen(refB, {S(L"stiffenerCreation", L"Full"), S(L"stiffenerMaterial", L"S355"),
                                              S(L"stiffenerName", L"RIB1"), S(L"stiffenerFinish", L"Galv"),
                                              S(L"stiffenerPosPrefix", L"S"), I(L"stiffenerPosStart", 7)}));
        check(s && s->material == L"S355" && s->name == L"RIB1" && s->finish == L"Galv" && s->posNo == L"S7",
              "material / name / finish / Pos_No plana tasinir");
    }

    // --- diyalog: Stiffeners alanlari artik motor parametresi, hicbiri gri degil ------------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        const ComponentType* t = reg.findType(L"tekla_shear_plate_146");
        const ComponentDialogDefinition d = shearPlate146DialogDefinition();
        const wchar_t* names[] = {L"stiffenerCreation", L"stiffenerOrientation", L"stiffenerT",
                                  L"stiffenerW", L"stiffenerH", L"stiffenerGapFlange", L"stiffenerGapWeb",
                                  L"stiffenerChamferType", L"stiffenerChamferH", L"stiffenerChamferV",
                                  L"stiffenerPosPrefix", L"stiffenerPosStart", L"stiffenerMaterial",
                                  L"stiffenerName", L"stiffenerFinish"};
        bool allEngine = (t != nullptr), noneGrey = true;
        for (const wchar_t* n : names) {
            bool inEngine = false;
            if (t) for (const auto& sp : t->params) if (sp.name == n) inEngine = true;
            bool grey = false;
            for (const auto& sp : d.specs) if (sp.name == n && sp.documentedOnly) grey = true;
            if (!inEngine) { allEngine = false; std::printf("  motor parametresi degil: %ls\n", n); }
            if (grey) { noneGrey = false; std::printf("  hala gri: %ls\n", n); }
        }
        check(allEngine, "Stiffeners alanlari motor parametresi");
        check(noneGrey, "Stiffeners alanlarinin hicbiri gri degil");
    }

    std::printf("%s (%d failure)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
