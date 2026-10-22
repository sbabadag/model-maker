// UNIVERSAL COMPONENT DIALOG + SCHEMATIC FRAMEWORK — portatif cekirdek testi.
//
// Kapsar: endPlateDialogSpecs (27 alan), EndPlateParams <-> CompParam adaptor
// round-trip, guvenli varsayilanlar, validateDialogSchema (tekil id + gecerli
// bag + bilinmeyen parametre reddi), hitTestDiagramHotspot (secim + en yakin +
// bos), findHotspot ve ComponentDialogRegistry (kayit/tekil/bul).
//
// Derleme (Linux, OCC'siz):
//   g++ -std=gnu++20 -O1 -I include tests/component_dialog_test.cpp \
//       build/libmodel_maker_core.a -o component_dialog_test && ./component_dialog_test

#include "model_maker/component_dialog.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static bool nearD(double a, double b, double tol = 1e-9) { return std::abs(a - b) < tol; }

int main() {
    // --- 1) End Plate dialog semasi: 27 alan, tekil ad ----------------------
    {
        const auto specs = endPlateDialogSpecs();
        check(specs.size() == 27, "end plate dialog semasi 27 alan");
        bool unique = true;
        for (std::size_t i = 0; i < specs.size() && unique; ++i)
            for (std::size_t j = i + 1; j < specs.size(); ++j)
                if (specs[i].name == specs[j].name) { unique = false; break; }
        check(unique, "end plate dialog semasi adlari tekil");
    }

    // --- 2) EndPlateParams -> CompParam -> EndPlateParams round-trip --------
    {
        EndPlateParams p;
        p.plateThickness = 14.0;
        p.plateExtra = 25.0;
        p.topExtra = 5.0;
        p.bottomExtra = 8.0;
        p.upperEdgeDistance = 12.0;
        p.lowerEdgeDistance = 15.0;
        p.plateWidthOverride = 220.0;
        p.plateHeightOverride = 180.0;
        p.plateMaterial = "S355";
        p.plateName = "LEVHA-1";
        p.plateFinish = "Boya";
        p.gap = 2.0;
        p.boltDiameter = 24.0;
        p.holeDiameter = 26.0;
        p.boltGrade = "10.9";
        p.boltCountX = 3;
        p.boltCountY = 4;
        p.boltEdge = 35.0;
        p.boltAlignH = 2;  // Sag
        p.boltAlignV = 0;  // Ust
        p.boltOffsetH = 10.0;
        p.boltOffsetV = -2.0;
        p.boltSpacingX = 90.0;
        p.boltSpacingY = 80.0;
        p.hasHead = false;
        p.hasNut = true;
        p.hasWasher = false;

        const auto params = endPlateToParams(p);
        check(params.size() == 27, "adaptor 27 parametre uretir");
        const EndPlateParams q = endPlateFromParams(params);
        check(q == p, "end plate adaptor round-trip kayipsiz");
    }

    // --- 3) Guvenli varsayilanlar: bos + gecersiz enum ----------------------
    {
        const EndPlateParams empty = endPlateFromParams({});
        check(nearD(empty.plateThickness, 12.0) && nearD(empty.boltDiameter, 20.0) &&
                  empty.boltCountX == 2 && empty.hasHead,
              "bos parametre listesi varsayilanlari verir");
        check(empty.boltGrade == "8.8" && empty.boltAlignH == 1 && empty.boltAlignV == 2,
              "bos liste enum varsayilanlarini verir (grade=8.8, H=Orta, V=Alt)");

        std::vector<CompParam> bogus;
        bogus.push_back({L"grade", std::wstring(L"99.9")});
        bogus.push_back({L"boltAlignH", std::wstring(L"X")});
        bogus.push_back({L"boltAlignV", std::wstring(L"Y")});
        const EndPlateParams r = endPlateFromParams(bogus);
        check(r.boltGrade == "8.8" && r.boltAlignH == 1 && r.boltAlignV == 2,
              "gecersiz enum degeri varsayilana duser");
    }

    // --- 4) Yerlesik semalar dogrulanir -------------------------------------
    {
        const auto ep = endPlateDialogDefinition();
        const auto fp = finPlateDialogDefinition();
        check(validateDialogSchema(ep.schema, ep.specs).ok, "end plate semasi gecerli");
        check(validateDialogSchema(fp.schema, fp.specs).ok, "fin plate semasi gecerli");
        // CompParamSpec == yok; adlari (sira + kume) karsilastir.
        {
            const auto engine = finPlateComponentType().params;
            bool same = fp.specs.size() == engine.size();
            for (std::size_t i = 0; same && i < engine.size(); ++i)
                if (fp.specs[i].name != engine[i].name) same = false;
            check(same, "fin plate dialog semasi engine semasiyla AYNI");
        }
    }

    // --- 5) Dogrulama hatalari yakalar --------------------------------------
    {
        const auto ep = endPlateDialogDefinition();

        // Hotspot bilinmeyen parametreye bagli.
        {
            auto bad = ep.schema;
            bad.hotspots[0].paramName = std::wstring(L"olmayan_param");
            const auto v = validateDialogSchema(bad, ep.specs);
            check(!v.ok && !v.problems.empty(), "bilinmeyen hotspot parametresi reddedilir");
        }
        // Yinelenen item id.
        {
            auto bad = ep.schema;
            bad.items[1].id = bad.items[0].id;
            const auto v = validateDialogSchema(bad, ep.specs);
            check(!v.ok, "yinelenen item id reddedilir");
        }
        // Grup bilinmeyen parametre isaret eder.
        {
            auto bad = ep.schema;
            bad.groups[0].paramNames.push_back(L"yok");
            const auto v = validateDialogSchema(bad, ep.specs);
            check(!v.ok, "grup bilinmeyen parametreyi isaret edemez");
        }
    }

    // --- 6) Hotspot hit-test -------------------------------------------------
    {
        const auto ep = endPlateDialogDefinition();
        const auto& s = ep.schema;

        auto hit = hitTestDiagramHotspot(s, DiagramPoint{74.0, 224.0});
        check(hit && *hit == L"hs_thickness", "kalınlik hotspot'u vurulur");

        const DiagramHotspot* h = hit ? findHotspot(s, *hit) : nullptr;
        check(h && h->paramName && *h->paramName == L"plateThickness",
              "vurulan hotspot plateThickness parametresine bagli");

        // En yakin kazanir: hs_bolt_count_v (74,130) ile hs_thickness (74,224) arasi.
        auto between = hitTestDiagramHotspot(s, DiagramPoint{74.0, 132.0});
        check(between && *between == L"hs_bolt_count_v", "en yakin hotspot kazanir");

        // Bos bolge.
        check(!hitTestDiagramHotspot(s, DiagramPoint{200.0, 200.0}).has_value(),
              "bos bolge hotspot secmez");
    }

    // --- 7) Tekla 146 duzeni: sekmeler + bulon ozellik alanlari ---------------
    {
        const ComponentDialogDefinition def = shearPlate146DialogDefinition();
        check(def.schema.useTabs, "146: sekmeli duzen");
        check(def.schema.groups.size() == 6, "146: 6 sekme");
        const wchar_t* expected[] = {L"Plates", L"Stiffeners", L"General",
                                     L"Notch", L"Bolts", L"Beam cut"};
        bool titlesOk = def.schema.groups.size() == 6;
        for (std::size_t i = 0; titlesOk && i < 6; ++i)
            titlesOk = def.schema.groups[i].title == expected[i] && def.schema.groups[i].asTab;
        check(titlesOk, "146: sekme adlari ve sirasi Tekla ile ayni");

        // Tekla "Bolt properties" alanlari semada VAR ve Default kutulu (optional).
        const wchar_t* teklaBolt[] = {L"boltStandard", L"tolerance", L"threadInMat",
                                      L"holeType", L"rotateSlots", L"slotsIn",
                                      L"cutLength", L"comment"};
        for (const wchar_t* name : teklaBolt) {
            const CompParamSpec* s = findParamSpec(def.specs, name);
            check(s != nullptr, "146: Tekla bulon alani semada var");
            check(s && s->optional, "146: Tekla bulon alani Default kutulu");
        }

        const auto v = validateDialogSchema(def.schema, def.specs);
        check(v.ok, "146: sekme semasi gecerli (hotspot/grup baglari)");
        // Sematik: 2x2 bulon deligi (4 daire) + olcu cizgileri.
        const ComponentDiagramItem* bolts = nullptr;
        for (const auto& it : def.schema.items)
            if (it.id == L"bolts") bolts = &it;
        check(bolts && bolts->primitives.size() == 4, "146: levhada 2x2 bulon deligi");
        bool hasDims = false;
        for (const auto& it : def.schema.items) hasDims = hasDims || it.id == L"dims";
        check(hasDims, "146: semada olcu cizgileri var");
    }

    // --- 8) Registry ---------------------------------------------------------
    {
        ComponentDialogRegistry reg;
        check(reg.count() == 0, "bos registry");
        registerBuiltInDialogs(reg);
        check(reg.count() == 3, "uc yerlesik dialog kayitli");
        check(reg.find(L"beam_to_column_end_plate") != nullptr, "end plate dialogu bulunur");
        check(reg.find(L"fin_plate") != nullptr, "fin plate dialogu bulunur");
        check(reg.find(L"tekla_shear_plate_146") != nullptr, "146 dialogu bulunur");
        check(reg.find(L"yok") == nullptr, "bilinmeyen tip bulunmaz");

        // Tekil kayit: ayni typeId reddedilir.
        check(!reg.registerDialog(endPlateDialogDefinition()), "yinelenen typeId reddedilir");
        // Bos typeId reddedilir.
        ComponentDialogDefinition empty;
        check(!reg.registerDialog(std::move(empty)), "bos typeId reddedilir");
        check(reg.count() == 3, "reddedilen kayitlar sayiyi artirmaz");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
