// TEKLA 146 (Shear plate simple) — cekirdek test.
//
// 146 = fin plate cekirdegi (tekli ortogonal shear tab) + Tekla 146'nin
// dogrulanabilir yuzeyi: cift tab, tab gap, kiris ucu kare kesimi + beam gap,
// bulon grubu yerlesimi (degisken aralik/olcu/silinen), stiffener, backing bar.
// Bu test dogrular:
//   1) 146 tipi yerlesik katalogda (3. tip), sema 19 parametre.
//   2) VARSAYILAN 146 (tek near tab) tab+bilon geometrisi fin plate ile AYNI
//      (ortak cekirdek); ek olarak kiris ucu kesimi uretir (roleKey'ler yeni).
//   3) Cift tab (near_and_far) simetrik iki levha uretir; far_side tek far.
//   4) tabGapSecondary levhayi kaydirir.
//   5) Kiris ucu kesimi: duzlem dogru, hedef kiris.
//   6) Bulon yerlesimi: acik aralik -> spacingYs; measure top/below; silinen.
//   7) Stiffener + backing bar kosullu uretilir.
//   8) Dialog tanimi sema-dogrulamadan gecer + hotspot hit-test calisir.
// OCC'siz, Linux'ta kosar.

#include "model_maker/component_engine.hpp"
#include "model_maker/component_dialog.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) < tol;
}
static bool sameV(const Vec3& a, const Vec3& b) {
    return approx(a.x, b.x) && approx(a.y, b.y) && approx(a.z, b.z);
}

// Standart ortogonal girdi: kolon dusey (300x300), kiris +Y (IPE200 -> 200x300).
static ComponentInput makeInput() {
    ComponentInput in;
    in.main.id = EntityId{20};                 // kolon
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = {0, 0, 1};
    in.main.secWidth = 300.0;
    in.main.secHeight = 300.0;
    MemberGeometry beam;
    beam.id = EntityId{21};                    // kiris
    beam.axisFrom = {0, 0, 3000};
    beam.axisTo = {0, 4000, 3000};             // +Y
    beam.refNormal = {0, 0, 1};
    beam.secWidth = 200.0;
    beam.secHeight = 300.0;
    in.secondary.push_back(beam);
    return in;
}

int main() {
    // --- 1) 146 tipi yerlesik katalogda --------------------------------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        check(reg.typeCount() == 3, "3 yerlesik component (end plate + fin plate + 146)");

        const ComponentType* t = reg.findType(L"tekla_shear_plate_146");
        check(t != nullptr, "146 tipi bulundu");
        if (t) {
            check(t->displayName == L"Shear Plate Simple (146)", "146 displayName");
            check(t->schemaVersion == 1, "146 schemaVersion 1");
            check(t->params.size() == 24, "146 semasi 24 parametre (8 core + 16 Tekla; cok kolonlu eklendi)");
        }
    }

    // --- 2) Varsayilan 146: tab+bilon == fin plate core; + kiris ucu kesimi ---
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        ComponentInput in = makeInput();

        GenerationPlan fp = reg.generate(L"fin_plate", in);
        GenerationPlan s146 = reg.generate(L"tekla_shear_plate_146", in);

        check(fp.ok, "fin plate uretildi (referans)");
        check(s146.ok, "146 uretildi");
        check(fp.plates.size() == 1 && s146.plates.size() == 1, "146: 1 levha (tek near tab)");
        check(fp.boltGroups.size() == 1 && s146.boltGroups.size() == 1, "146: 1 bulon grubu");
        check(s146.cuts.size() == 1, "146: 1 kiris ucu kesimi");

        if (fp.plates.size() == 1 && s146.plates.size() == 1) {
            const PlateSpec& a = fp.plates[0];
            const PlateSpec& b = s146.plates[0];
            check(sameV(a.center, b.center), "146 levha merkezi == fin plate");
            check(sameV(a.axis, b.axis) && sameV(a.u, b.u) && sameV(a.v, b.v),
                  "146 levha cercevesi == fin plate");
            check(approx(a.width, b.width) && approx(a.height, b.height) &&
                      approx(a.thickness, b.thickness),
                  "146 levha boyutlari == fin plate");
        }
        if (fp.boltGroups.size() == 1 && s146.boltGroups.size() == 1) {
            const BoltGroup& b = s146.boltGroups[0];
            check(b.parts.size() == 1 && b.parts[0] == EntityId{21},
                  "146 bulonlari KIRISE referansli (kolona degil)");
            check(b.countY == 4, "146 varsayilan 4 bulon");
        }
        // roleKey'ler.
        check(s146.plateKeys.size() == 1 && s146.plateKeys[0] == "shear_tab_near",
              "146 levha roleKey = shear_tab_near");
        check(s146.boltGroupKeys.size() == 1 && s146.boltGroupKeys[0] == "beam_bolts",
              "146 bulon roleKey = beam_bolts");
        check(s146.cutKeys.size() == 1 && s146.cutKeys[0] == "cut_beam_end",
              "146 kesim roleKey = cut_beam_end");
        // Kiris ucu kesimi: yuz + beamGap(20) yonunde, normal = lenDir, korunan = +taraf.
        if (s146.cuts.size() == 1) {
            const CutModifier& c = s146.cuts[0];
            check(c.kind == CutKind::PlaneCut, "kesim turu PlaneCut");
            check(c.target == EntityId{21}, "kesim hedefi = kiris");
            // facePoint = (0,150,3000); lenDir=(0,1,0); beamGap=20 -> (0,170,3000).
            check(sameV(c.planeOrigin, Vec3{0.0, 170.0, 3000.0}), "kesim duzlemi = yuz+gap");
            check(sameV(c.planeNormal, Vec3{0.0, 1.0, 0.0}), "kesim normali = lenDir");
            check(c.keepPositive, "korunan taraf = kiris govdesi");
        }
    }

    // --- 3) Cift tab (near_and_far) + far_side -------------------------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);

        ComponentInput in = makeInput();
        in.params = {{L"shearTabPosition", CompParamValue{std::wstring(L"near_and_far")}}};
        GenerationPlan plan = reg.generate(L"tekla_shear_plate_146", in);
        check(plan.ok, "146 (cift tab) uretildi");
        check(plan.plates.size() == 2, "cift tab: 2 levha");
        if (plan.plates.size() == 2) {
            // Near tab center X = -10 (sec.width=(-1,0,0), ecc=10); far tab +10.
            check(approx(plan.plates[0].center.x, -10.0) && approx(plan.plates[1].center.x, 10.0),
                  "cift tab simetrik (near -10 / far +10)");
            check(sameV(plan.plates[0].center, Vec3{-10.0, 210.0, 3000.0}) ||
                      sameV(plan.plates[1].center, Vec3{-10.0, 210.0, 3000.0}),
                  "near tab konumu dogru");
        }
        check(plan.plateKeys.size() == 2 &&
                  plan.plateKeys[0] == "shear_tab_near" && plan.plateKeys[1] == "shear_tab_far",
              "cift tab roleKey'leri");

        ComponentInput inFar = makeInput();
        inFar.params = {{L"shearTabPosition", CompParamValue{std::wstring(L"far_side")}}};
        GenerationPlan planFar = reg.generate(L"tekla_shear_plate_146", inFar);
        check(planFar.ok && planFar.plates.size() == 1, "far_side: 1 far tab");
        if (planFar.plates.size() == 1) {
            check(approx(planFar.plates[0].center.x, 10.0), "far tab +10 (karsi taraf)");
            check(planFar.plateKeys[0] == "shear_tab_far", "far tab roleKey");
        }
    }

    // --- 4) tabGapSecondary levhayi kaydirir ---------------------------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        ComponentInput in = makeInput();
        in.params = {{L"tabGapSecondary", CompParamValue{6.0}}};
        GenerationPlan plan = reg.generate(L"tekla_shear_plate_146", in);
        check(plan.ok && plan.plates.size() == 1, "146 (gap) uretildi");
        if (plan.plates.size() == 1) {
            // ecc = thickness + gap = 10 + 6 = 16; sec.width=(-1,0,0) -> X = -16.
            check(approx(plan.plates[0].center.x, -16.0), "tab gap levhayi kaydirir (-16)");
        }
    }

    // --- 5) Bulon yerlesimi: acik aralik + measure + silinen ------------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);

        // Acik aralik "60 60 60" (4 bulon -> 3 aralik), measure middle.
        ComponentInput in = makeInput();
        in.params = {{L"boltSpacing", CompParamValue{std::wstring(L"60 60 60")}},
                     {L"deletedBolts", CompParamValue{std::wstring(L"1 3")}}};
        GenerationPlan plan = reg.generate(L"tekla_shear_plate_146", in);
        check(plan.ok && plan.boltGroups.size() == 1, "146 (acik aralik) uretildi");
        if (plan.boltGroups.size() == 1) {
            const BoltGroup& b = plan.boltGroups[0];
            check(b.spacingYs.size() == 3 && approx(b.spacingYs[0], 60.0) &&
                      approx(b.spacingYs[2], 60.0),
                  "acik aralik -> spacingYs (3 x 60)");
            check(b.deletedBolts.size() == 2 && b.deletedBolts[0] == 1 && b.deletedBolts[1] == 3,
                  "silinen bulonlar [1,3]");
            // span = 180; middle origin Z = 3000 - 90 = 2910.
            check(approx(b.origin.z, 2910.0), "middle: origin Z = 2910");
        }

        // measure top (span 180): origin Z = 3000 + (hh-edge=80) - 180 = 2900.
        ComponentInput inTop = makeInput();
        inTop.params = {{L"boltSpacing", CompParamValue{std::wstring(L"60 60 60")}},
                        {L"boltVerticalMeasure", CompParamValue{std::wstring(L"top")}}};
        GenerationPlan pTop = reg.generate(L"tekla_shear_plate_146", inTop);
        check(pTop.ok && pTop.boltGroups.size() == 1 && approx(pTop.boltGroups[0].origin.z, 2900.0),
              "top: origin Z = 2900");

        // measure below (span 180): origin Z = 3000 - (hh-edge=80) = 2920.
        ComponentInput inBelow = makeInput();
        inBelow.params = {{L"boltSpacing", CompParamValue{std::wstring(L"60 60 60")}},
                          {L"boltVerticalMeasure", CompParamValue{std::wstring(L"below")}}};
        GenerationPlan pBelow = reg.generate(L"tekla_shear_plate_146", inBelow);
        check(pBelow.ok && pBelow.boltGroups.size() == 1 && approx(pBelow.boltGroups[0].origin.z, 2920.0),
              "below: origin Z = 2920");
    }

    // --- 6) Stiffener + backing bar kosullu ----------------------------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);

        ComponentInput in = makeInput();
        in.params = {{L"stiffenerCreation", CompParamValue{std::wstring(L"full")}},
                     {L"backingBarT", CompParamValue{8.0}}};
        GenerationPlan plan = reg.generate(L"tekla_shear_plate_146", in);
        check(plan.ok, "146 (stiffener + backing bar) uretildi");
        // 1 near tab + 1 stiffener + 1 backing bar = 3 levha.
        check(plan.plates.size() == 3, "3 levha (tab + stiffener + backing bar)");
        // Stiffener roleKey + konum (kolon ekseni x=0,y=0, kiris kotu z=3000).
        bool hasStiff = false, hasBacking = false;
        for (std::size_t i = 0; i < plan.plateKeys.size(); ++i) {
            if (plan.plateKeys[i] == "stiffener") {
                hasStiff = true;
                check(sameV(plan.plates[i].center, Vec3{0.0, 0.0, 3000.0}),
                      "stiffener kolon ekseninde + kiris kotunda");
            }
            if (plan.plateKeys[i] == "backing_bar") {
                hasBacking = true;
                check(approx(plan.plates[i].thickness, 8.0), "backing bar kalinligi 8");
            }
        }
        check(hasStiff && hasBacking, "stiffener + backing bar roleKey'leri mevcut");
    }

    // --- 7) Parametre degisimi geometriye yansir ------------------------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        ComponentInput in = makeInput();
        in.params = {{L"plateThickness", CompParamValue{14.0}},
                     {L"boltCount", CompParamValue{6}}};
        GenerationPlan plan = reg.generate(L"tekla_shear_plate_146", in);
        check(plan.ok, "146 (degisken parametre) uretildi");
        if (plan.plates.size() == 1) check(approx(plan.plates[0].thickness, 14.0),
                                            "146 kalinlik parametreden");
        if (plan.boltGroups.size() == 1) check(plan.boltGroups[0].countY == 6,
                                                "146 6 bulon (parametreden)");
    }

    // --- 8) Dialog tanimi sema-dogrulamadan gecer -----------------------------
    {
        const ComponentDialogDefinition def = shearPlate146DialogDefinition();
        check(def.typeId == L"tekla_shear_plate_146", "146 dialog typeId");
        check(def.schema.typeId == def.typeId, "146 dialog schema.typeId dolu");

        const DialogSchemaValidation v = validateDialogSchema(def.schema, def.specs);
        check(v.ok, "146 dialog semasi gecerli");
        if (!v.ok) {
            for (const auto& p : v.problems)
                std::printf("  problem: %ls\n", p.c_str());
        }
        // Tum grup parametreleri semada olmali.
        check(def.specs.size() == 24, "146 dialog specs = 24 parametre");

        const auto hit = hitTestDiagramHotspot(def.schema, DiagramPoint{63.0, 192.0});
        check(hit.has_value(), "146 hotspot hit-test calisir");
        if (hit.has_value()) {
            const DiagramHotspot* h = findHotspot(def.schema, *hit);
            check(h && h->paramName.has_value() && *h->paramName == L"plateThickness",
                  "146 hotspot -> plateThickness");
        }
    }

    std::printf("FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
