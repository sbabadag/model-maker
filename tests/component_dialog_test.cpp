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
        check(def.schema.groups.size() == 13, "146: 13 sekme (Tekla ekran goruntusu)");
        const wchar_t* expected[] = {L"Picture", L"Plates", L"Stiffeners", L"General",
                                     L"Haunch", L"Notch", L"Bolts", L"Design type",
                                     L"Beam cut", L"Angle box", L"BoxPBolts", L"BoxSBolts",
                                     L"Analysis"};
        bool titlesOk = def.schema.groups.size() == 13;
        for (std::size_t i = 0; titlesOk && i < 13; ++i)
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

        // Tekla'da olup geometrisi olmayan alanlar: documentedOnly, motor
        // parametreleriyle (ComponentType) ASLA cakismaz ve motor semasina girmez.
        {
            const auto engineParams = shearPlate146ComponentType().params;
            std::size_t docCount = 0;
            bool noClash = true, engineNotDoc = true;
            for (const auto& s : def.specs) {
                const bool inEngine = findParamSpec(engineParams, s.name) != nullptr;
                if (s.documentedOnly) {
                    ++docCount;
                    if (inEngine) noClash = false;
                } else if (!inEngine) {
                    engineNotDoc = false;  // gri olmayan alan motorda olmali
                }
            }
            check(docCount >= 60, "146: Tekla gri (documentedOnly) alanlari mevcut");
            check(noClash, "146: gri alan motor parametresiyle cakismaz");
            check(engineNotDoc, "146: gri olmayan her alan motor parametresi");
            for (const wchar_t* n : {L"haunchCreation", L"notchShape", L"tabOrientation",
                                     L"plateWasherType", L"flangeStripSize"}) {
                const CompParamSpec* s = findParamSpec(def.specs, n);
                check(s && s->documentedOnly, "146: Tekla alani gri olarak var");
            }
            for (const wchar_t* n : {L"plateThickness", L"beamGap", L"stiffenerCreation",
                                     L"boltCount", L"shearTabPosition", L"backingBarT"}) {
                const CompParamSpec* s = findParamSpec(def.specs, n);
                check(s && !s->documentedOnly, "146: calisan alan gri degil");
            }
        }

        // Tekla parca satirlari: Plates 2 (Tab plate, Plate washer), Stiffeners 1,
        // Haunch 2 (Top/Bottom), Beam cut 1 (Backing bar), Angle box 3.
        {
            const auto rowsOf = [&](const wchar_t* title) -> std::size_t {
                for (const auto& g : def.schema.groups)
                    if (g.title == title) return g.partRows.size();
                return 999;
            };
            check(rowsOf(L"Plates") == 2, "146: Plates 2 parca satiri");
            check(rowsOf(L"Stiffeners") == 1, "146: Stiffeners 1 parca satiri");
            check(rowsOf(L"Haunch") == 2, "146: Haunch ust+alt plaka satiri");
            check(rowsOf(L"Beam cut") == 1, "146: Beam cut backing bar satiri");
            check(rowsOf(L"Angle box") == 3, "146: Angle box 3 parca satiri");
            // General / Design type: ekran goruntusundeki alanlar GRI olarak var.
            for (const wchar_t* n : {L"upDirection", L"locked", L"classNo", L"connectionCode",
                                     L"autoDefaultsGroup", L"autoConnectionGroup",
                                     L"externalDesign", L"checkConnection", L"designCode",
                                     L"loadDefinition", L"udlPercent", L"loadShear",
                                     L"loadTension", L"loadMoment"}) {
                const CompParamSpec* sp = findParamSpec(def.specs, n);
                check(sp && sp->documentedOnly, "146: General/Design type alani gri olarak var");
            }
            const CompParamSpec* cls = findParamSpec(def.specs, L"classNo");
            check(cls && cls->defInt == 99, "146: Class varsayilani 99 (ekran goruntusu)");
            // Manuel notch: ust ve alt flans AYRI alan seti.
            for (const wchar_t* n : {L"flangeNotchSideTop", L"flangeNotchSideBottom",
                                     L"notchCutHTop", L"notchCutHBottom",
                                     L"notchWebToFlangeTop", L"notchWebToFlangeBottom"})
                check(findParamSpec(def.specs, n) != nullptr, "146: manuel notch ust/alt alani var");
            // Analysis (gercek ekran goruntusu): 13 satir, Ux..Rz "a|b" ciftli.
            const auto& last = def.schema.groups.back();
            check(last.title == L"Analysis" && last.paramNames.size() == 13,
                  "146: Analysis sekmesi 13 satir");
            std::size_t pairs = 0;
            for (const auto& pn : last.paramNames)
                if (pn.find(L'|') != std::wstring::npos) ++pairs;
            check(pairs == 6, "146: Analysis Ux/Uy/Uz/Rx/Ry/Rz 6 cift satir");
            const CompParamSpec* sup = findParamSpec(def.specs, L"supportRx");
            check(sup && sup->documentedOnly && sup->optional &&
                      sup->choices.size() == 1 && sup->choices[0] == L"Pinned",
                  "146: Rx = Pinned (ekran goruntusu), gri, Default kutulu");
            // BoxPBolts / BoxSBolts: ozel yerlesim, ayni 29 alan, onek ayrimi.
            for (const wchar_t* title : {L"BoxPBolts", L"BoxSBolts"}) {
                const ComponentDialogGroup* bg = nullptr;
                for (const auto& g : def.schema.groups)
                    if (g.title == title) bg = &g;
                check(bg && bg->layout == L"boxbolts" && bg->paramNames.size() == 24,
                      "146: BoxP/BoxS boxbolts yerlesimi, 24 alan");
                if (bg) {
                    const CompParamSpec* hole = findParamSpec(def.specs, bg->prefix + L"TopHoleType");
                    check(hole && hole->documentedOnly && hole->optional,
                          "146: BoxP/BoxS Top hole type gri + Default kutulu");
                    check(findParamSpec(def.specs, bg->prefix + L"BottomSlotsIn") != nullptr,
                          "146: BoxP/BoxS Bottom alani var");
                }
            }
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

        // KAYNAK TABLOSU (Tekla "Welds"): 10 satir + 10 numarali cagri.
        check(def.schema.weldRows.size() == 10, "146: 10 kaynak satiri");
        bool rowsOk = def.schema.weldRows.size() == 10;
        for (std::size_t i = 0; rowsOk && i < def.schema.weldRows.size(); ++i) {
            const WeldTableRow& r = def.schema.weldRows[i];
            rowsOk = r.number == static_cast<int>(i) + 1 && r.size > 0.0;
        }
        check(rowsOk, "146: kaynak satirlari sirali ve olculu");

        int callouts = 0;
        for (const auto& h : def.schema.hotspots)
            if (h.weldRow.has_value()) ++callouts;
        check(callouts == 10, "146: 10 kaynak cagrisi (hotspot)");
        // Cagrilar satirlarla eslesir: label == satir numarasi.
        bool calloutOk = true;
        for (const auto& h : def.schema.hotspots) {
            if (!h.weldRow.has_value()) continue;
            const int row = *h.weldRow;
            if (row < 0 || row >= static_cast<int>(def.schema.weldRows.size())) { calloutOk = false; break; }
            if (!h.label.has_value()) { calloutOk = false; break; }
            if (*h.label != std::to_wstring(def.schema.weldRows[row].number)) { calloutOk = false; break; }
        }
        check(calloutOk, "146: cagri etiketi satir numarasiyla ayni");
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
