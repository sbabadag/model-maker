// H-8 SONRASI HAUNCH TOP-ANKRAJ REGRESYONU (Selahattin 2026-11-09).
//
// GT-1 t11'in kopyasi: HEB240 kolon (Top ankraj, eksen x=0, govde x∈[-240,0])
// + IPE300 egik kiris (Top ankraj, ust cizgi z=7000..7500, govde cizginin
// ALTINDA). KOK NEDEN: resolveShearTabPlacement / haunchFrameAnalysis, kiris
// ankraj kaymasini YENIDEN kurulan p.sec (lenDir + worldUp) cercevesinde
// uyguluyordu; kanoniklestirme refNormal'i cevirince (flippedNormal)
// sectionCenterOffset isaret degistiriyor ve kayma TERS yone gidiyordu ->
// haunch govdesi kirisin ICINE giriyor, alin levhasi kolon tepesinin ustune
// cikiyor, varsayilan parametrelerde (240/25) iki bulon grubu kuculen
// levhaya sigmiyordu ("iki bulon grubu alin levhasina sigmiyor").
//
// Beklenen (duzeltilmis) sayilar:
//   - varsayilan parametrelerle plan OK (red YOK)
//   - alin levhasi kolon yuzune oturur: x ∈ [120, 140] (gap=0, t=20)
//   - levha ustu kolon tepesine kripilir: z_ust ≈ 7000 (ustune tasmaz)
//   - levha alti kiriş alti + haunch derinligini kapsar: z_alt ≤ 6460
//   - haunch govdesi KIRISIN ALTINDA: govde merkez z < 6700 (kiris alt basligi)
//   - kesim duzlemi kiris govdesini keser: x=140, z ∈ [6800, 6900]
#include "model_maker/component_engine.hpp"
#include "model_maker/component_input_builder.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/profile_database.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace mm;

static int bad = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    bad += ok ? 0 : 1;
}
static bool near(double a, double b, double tol = 1.0) {
    return std::fabs(a - b) <= tol;
}

static std::vector<SteelProfile> catalog() {
    // ctest CWD=build-hermes; elden kosumda repo koku. Iki adayi da dene.
    for (const char* p : {"build-hermes/profiles/european.lis",
                          "profiles/european.lis"}) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec))
            return parseTeklaProfileDatabase(std::filesystem::path(p));
    }
    return {};
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const auto cat = catalog();

    EntityProperties colProps;
    colProps.profileName = "HEB240";
    colProps.axisFromX = 0.0; colProps.axisFromY = 0.0; colProps.axisFromZ = 0.0;
    colProps.axisToX = 0.0; colProps.axisToY = 0.0; colProps.axisToZ = 7000.0;

    EntityProperties beamProps;
    beamProps.profileName = "IPE300";
    beamProps.axisFromX = 0.0; beamProps.axisFromY = 0.0; beamProps.axisFromZ = 7000.0;
    beamProps.axisToX = 6000.0; beamProps.axisToY = 0.0; beamProps.axisToZ = 7500.0;

    MemberGeometry col = memberGeometryFromEntityProperties(colProps, cat);
    MemberGeometry beam = memberGeometryFromEntityProperties(beamProps, cat);
    col.id = 1;
    beam.id = 2;
    // Top ankraj dogrulamasi: kayma sifir olmamali, kanonik yonlerde tasinmali.
    check(near(col.sectionCenterOffset, -120.0), "kolon Top ankraj ofseti -120");
    check(near(std::fabs(beam.sectionCenterOffset), 150.0),
          "kiris Top ankraj ofseti ±150 (kanonik isaretle)");

    ComponentInput in;
    in.main = col;
    in.secondary.push_back(beam);
    // Varsayilan parametreler: params BOS. Regresyon: eskiden
    // "iki bulon grubu alin levhasina sigmiyor" ile RED ediliyordu.
    const GenerationPlan plan =
        ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
    check(plan.ok, "varsayilan parametrelerle plan uretildi (red yok)");
    if (!plan.ok) {
        std::printf("       error=%ls\n", plan.error.c_str());
        return 1;
    }

    // Alin levhasi
    std::size_t endPlate = plan.plates.size();
    for (std::size_t i = 0; i < plan.plateKeys.size(); ++i)
        if (plan.plateKeys[i] == "haunch_end_plate") endPlate = i;
    check(endPlate < plan.plates.size(), "alin levhasi planda var");
    if (endPlate < plan.plates.size()) {
        const PlateSpec& p = plan.plates[endPlate];
        const double front = p.center.x - std::fabs(p.axis.x) * p.thickness * 0.5;
        const double back = p.center.x + std::fabs(p.axis.x) * p.thickness * 0.5;
        const double top = p.center.z + p.height * 0.5;
        const double bottom = p.center.z - p.height * 0.5;
        std::printf("       levha x[%.2f %.2f] z[%.2f %.2f] t=%.1f\n", front, back,
                    bottom, top, p.thickness);
        check(near(front, 120.0), "alin levhasi kolon yuzune oturur (x=120)");
        check(near(back, 140.0), "alin levhasi arka yuzu x=140");
        check(near(top, 7000.0, 1.5), "levha ustu kolon tepesinde (z=7000)");
        check(bottom <= 6460.0 + 1.0,
              "levha kiriş alti + haunch derinligini kapsar (z_alt <= 6460)");
    }

    // Haunch govdesi kirisin ALTINDA (kiris alt basligi z=6700).
    std::size_t web = plan.plates.size();
    for (std::size_t i = 0; i < plan.plateKeys.size(); ++i)
        if (plan.plateKeys[i] == "haunch_web") web = i;
    check(web < plan.plates.size(), "haunch govdesi planda var");
    if (web < plan.plates.size()) {
        const PlateSpec& p = plan.plates[web];
        const double top = p.center.z + p.height * 0.5;
        std::printf("       govde z_merkez=%.2f z_ust=%.2f\n", p.center.z, top);
        check(p.center.z < 6700.0, "haunch govdesi kiris alt basliginin altinda");
        check(top <= 6760.0, "govde ustu kiris govdesi bolgesinde kalir (z<=6760)");
    }

    // Kesim duzlemi: x=140 (levha arka yuzu), kiris govdesi hizasinda.
    check(plan.cuts.size() == 1, "tek kiris kesimi uretilir");
    if (!plan.cuts.empty()) {
        const CutModifier& c = plan.cuts.front();
        std::printf("       kesim o=(%.2f %.2f %.2f) n=(%.2f %.2f %.2f)\n",
                    c.planeOrigin.x, c.planeOrigin.y, c.planeOrigin.z,
                    c.planeNormal.x, c.planeNormal.y, c.planeNormal.z);
        check(near(c.planeOrigin.x, 140.0), "kesim duzlemi x=140");
        check(near(c.planeNormal.x, 1.0) && near(c.planeNormal.y, 0.0) &&
                  near(c.planeNormal.z, 0.0), "kesim duzlemi DUSEY");
        check(c.planeOrigin.z >= 6800.0 && c.planeOrigin.z <= 6900.0,
              "kesim duzlemi kiris govdesi hizasinda (6800..6900)");
    }

    // Iki bulon grubu levha icinde.
    check(plan.boltGroups.size() == 2, "iki bulon grubu uretilir");
    for (const auto& bg : plan.boltGroups)
        check(bg.origin.z > 6400.0 && bg.origin.z < 6950.0,
              "bulon grubu levha sinirlari icinde");

    return bad == 0 ? 0 : 1;
}
