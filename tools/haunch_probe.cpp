// Haunch 40 görsel şikayeti teşhis sondası (v2): uygulamanın KENDİ girdi
// kurucusunu (memberGeometryFromEntityProperties + european.lis katalogu)
// kullanır. Varsayılan parametrelerle üretimi ve levha yerleşimini döker.
#include "model_maker/component_engine.hpp"
#include "model_maker/component_input_builder.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/profile_database.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace mm;

static std::vector<SteelProfile> catalog() {
    auto p = std::filesystem::current_path() / "build-hermes" / "profiles" / "european.lis";
    return parseTeklaProfileDatabase(p);
}

static void dumpPlan(const char* label, const ComponentInput& in) {
    const GenerationPlan plan =
        ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
    std::printf("== %s ==\n", label);
    std::printf("ok=%d err=%ls\n", plan.ok ? 1 : 0, plan.error.c_str());
    for (const auto& c : plan.checks)
        std::printf("  check[%s] pass=%d warn=%d val=%.2f lim=%.2f msg=%ls\n",
                    c.id.c_str(), c.pass ? 1 : 0, c.warning ? 1 : 0, c.value,
                    c.limit, c.message.c_str());
    for (std::size_t i = 0; i < plan.plates.size(); ++i) {
        const PlateSpec& p = plan.plates[i];
        std::printf("  plate[%s] c=(%.1f %.1f %.1f) w=%.1f h=%.1f t=%.1f u=(%.3f %.3f %.3f) v=(%.3f %.3f %.3f)\n",
                    plan.plateKeys[i].c_str(), p.center.x, p.center.y, p.center.z,
                    p.width, p.height, p.thickness, p.u.x, p.u.y, p.u.z,
                    p.v.x, p.v.y, p.v.z);
    }
    for (const auto& c : plan.cuts)
        std::printf("  cut tgt=%llu o=(%.1f %.1f %.1f) n=(%.3f %.3f %.3f)\n",
                    (unsigned long long)c.target, c.planeOrigin.x, c.planeOrigin.y,
                    c.planeOrigin.z, c.planeNormal.x, c.planeNormal.y, c.planeNormal.z);
    for (const auto& bg : plan.boltGroups)
        std::printf("  bolts o=(%.1f %.1f %.1f) cx=%d cy=%d d=%.1f\n", bg.origin.x,
                    bg.origin.y, bg.origin.z, bg.countX, bg.countY,
                    bg.params.holeDiameter);
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const auto cat = catalog();
    std::printf("katalog: %zu profil\n", cat.size());

    // GT-1 t11 kurulumu: kolon HEB240 (0,0,0)-(0,0,7000), kiriş IPE300
    // (0,0,7000)-(6000,0,7500). Varsayılan ankraj Top, rotasyon 0.
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
    col.id = 1;      // uygulama: document_.modelId(index)
    beam.id = 2;
    std::printf("kolon  sec %dx%d tw=%.1f tf=%.1f rot=%.1f off=%.1f\n",
                (int)col.secWidth, (int)col.secHeight, col.secWebThickness,
                col.secFlangeThickness, col.profileRotation, col.sectionCenterOffset);
    std::printf("kiris  sec %dx%d tw=%.1f tf=%.1f rot=%.1f off=%.1f\n",
                (int)beam.secWidth, (int)beam.secHeight, beam.secWebThickness,
                beam.secFlangeThickness, beam.profileRotation, beam.sectionCenterOffset);

    ComponentInput in;
    in.main = col;
    in.secondary.push_back(beam);
    dumpPlan("Varsayilan (240/25)", in);

    ComponentInput in2 = in;
    in2.params = {{L"haunchDepth", CompParamValue{500.0}},
                  {L"haunchTipDepth", CompParamValue{30.0}}};
    dumpPlan("500/30", in2);

    return 0;
}
