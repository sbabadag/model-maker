// TEKLA 146 (Shear plate simple) — cekirdek test.
//
// 146 pilotu: tekli ortogonal shear tab = mevcut fin plate cekirdegi (kolon
// yuzune kaynakli, kiris govdesine civatali). Bu test dogrular:
//   1) 146 tipi yerlesik katalogda (3. tip) ve semasi 8 parametre.
//   2) generateShearPlateSimple146 ayni girdide fin plate ile BIREBIR ayni plani
//      uretir (delegasyon — paralel motor degil).
//   3) Parametre degisimi (boltCount, plateThickness) geometriye yansir.
//   4) Dialog tanimi sema-dogrulamadan gecer + hotspot hit-test calisir.
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
            check(t->params.size() == 8, "146 semasi 8 parametre (fin plate core)");
        }
    }

    // --- 2) 146 plani fin plate planiyla BIREBIR ayni (delegasyon) ------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);

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

        GenerationPlan fp = reg.generate(L"fin_plate", in);
        GenerationPlan s146 = reg.generate(L"tekla_shear_plate_146", in);

        check(fp.ok, "fin plate uretildi (referans)");
        check(s146.ok, "146 uretildi");
        check(fp.plates.size() == 1 && s146.plates.size() == 1, "146: 1 levha");
        check(fp.boltGroups.size() == 1 && s146.boltGroups.size() == 1, "146: 1 bulon grubu");

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
            const BoltGroup& a = fp.boltGroups[0];
            const BoltGroup& b = s146.boltGroups[0];
            check(sameV(a.origin, b.origin), "146 bulon origin == fin plate");
            check(a.countX == b.countX && a.countY == b.countY, "146 bulon adedi == fin plate");
            check(b.parts.size() == 1 && b.parts[0] == EntityId{21},
                  "146 bulonlari KIRISE referansli (kolona degil)");
        }

        // roleKey'ler korunur (fin plate cekirdegi).
        check(s146.plateKeys.size() == 1 && s146.plateKeys[0] == "fin_plate",
              "146 levha roleKey = fin_plate");
        check(s146.boltGroupKeys.size() == 1 && s146.boltGroupKeys[0] == "beam_bolts",
              "146 bulon roleKey = beam_bolts");
    }

    // --- 3) Parametre degisimi geometriye yansir ------------------------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);

        ComponentInput in;
        in.main.id = EntityId{20};
        in.main.axisFrom = {0, 0, 0};
        in.main.axisTo = {0, 0, 3000};
        in.main.refNormal = {0, 0, 1};
        in.main.secWidth = 300.0;
        in.main.secHeight = 300.0;
        MemberGeometry beam;
        beam.id = EntityId{21};
        beam.axisFrom = {0, 0, 3000};
        beam.axisTo = {0, 4000, 3000};
        beam.refNormal = {0, 0, 1};
        beam.secWidth = 200.0;
        beam.secHeight = 300.0;
        in.secondary.push_back(beam);
        in.params = {{L"plateThickness", CompParamValue{14.0}},
                     {L"boltCount", CompParamValue{6}}};

        GenerationPlan plan = reg.generate(L"tekla_shear_plate_146", in);
        check(plan.ok, "146 (degisken parametre) uretildi");
        if (plan.plates.size() == 1) {
            check(approx(plan.plates[0].thickness, 14.0), "146 kalinlik parametreden");
        }
        if (plan.boltGroups.size() == 1) {
            check(plan.boltGroups[0].countY == 6, "146 6 bulon (parametreden)");
        }
    }

    // --- 4) Dialog tanimi sema-dogrulamadan gecer -----------------------------
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

        // Hotspot hit-test: kalinlik hotspot'unu (63, 192) vurmali.
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
