// CAP LEVHASI — KOLON USTU BOSLUGU (topPlateGap) — Selahattin 2026-11-09.
//
// Kullanıcı: "haunch'ta cap plakası ile kolon üstü için bir parametre olmalı".
// Önceki durum: cap levhası her zaman kolon tepesine yapıştırılıyordu
// (merkez = kolon üstü + t/2); tesviye/shim payı verilemiyordu.
// Yeni davranış: merkez = kolon üstü + gap + t/2. gap = 0 -> ESKİ davranış
// birebir (regresyon yok); gap > 0 -> levha bosluk kadar yukari kalkar.
//
// Bu test: (1) parametre semada var ve varsayilani 0, (2) gap=0 ile cap
// levhasi kolon tepesine oturur, (3) gap=25 + t=12 ile levha tam 25 mm
// yukarida, (4) cap boslugu diger levhalari (alin levhasi) KAYDIRMAZ.
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
static bool near(double a, double b, double tol = 0.5) {
    return std::fabs(a - b) <= tol;
}

static std::vector<SteelProfile> catalog() {
    for (const char* p : {"build-hermes/profiles/european.lis", "profiles/european.lis"}) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec))
            return parseTeklaProfileDatabase(std::filesystem::path(p));
    }
    return {};
}

static ComponentInput makeInput() {
    EntityProperties colProps;
    colProps.profileName = "HEB240";
    colProps.axisFromX = 0.0; colProps.axisFromY = 0.0; colProps.axisFromZ = 0.0;
    colProps.axisToX = 0.0; colProps.axisToY = 0.0; colProps.axisToZ = 7000.0;

    EntityProperties beamProps;
    beamProps.profileName = "IPE300";
    beamProps.axisFromX = 0.0; beamProps.axisFromY = 0.0; beamProps.axisFromZ = 7000.0;
    beamProps.axisToX = 6000.0; beamProps.axisToY = 0.0; beamProps.axisToZ = 7500.0;

    const auto cat = catalog();
    ComponentInput in;
    in.main = memberGeometryFromEntityProperties(colProps, cat);
    in.secondary.push_back(memberGeometryFromEntityProperties(beamProps, cat));
    in.main.id = 1;
    in.secondary[0].id = 2;
    return in;
}

static const PlateSpec* findPlate(const GenerationPlan& p, const char* key, bool* found) {
    for (std::size_t i = 0; i < p.plateKeys.size(); ++i)
        if (p.plateKeys[i] == key) { *found = true; return &p.plates[i]; }
    *found = false;
    return nullptr;
}

static CompParam num(const wchar_t* name, double v) {
    CompParam p;
    p.name = name;
    p.value = v;
    return p;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    // 1) Parametre semada: ad, birim, varsayilan.
    const ComponentType* type =
        ComponentRegistry::instance().findType(L"tekla_haunch_40");
    check(type != nullptr, "haunch 40 tipi kayitli");
    bool specFound = false;
    if (type) {
        for (const CompParamSpec& s : type->params) {
            if (s.name != L"topPlateGap") continue;
            specFound = true;
            check(s.type == CompParamType::Length, "topPlateGap Length tipinde");
            check(near(s.defDouble, 0.0), "topPlateGap varsayilani 0 (eski davranis)");
            check(s.unit == L"mm", "topPlateGap birimi mm");
        }
    }
    check(specFound, "topPlateGap parametre semasinda VAR (diyalogda gorunur)");

    // 2) gap = 0: cap levhasi kolon tepesine oturur (t = 10).
    {
        ComponentInput in = makeInput();
        const GenerationPlan plan =
            ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
        check(plan.ok, "gap=0: plan uretildi");
        if (plan.ok) {
            bool found = false;
            const PlateSpec* cap = findPlate(plan, "haunch_top_plate", &found);
            check(found, "gap=0: cap levhasi planda var");
            if (cap) {
                const double bottom = cap->center.z - cap->thickness * 0.5;
                std::printf("       gap=0: t=%.1f merkez_z=%.2f alt=%.2f\n",
                            cap->thickness, cap->center.z, bottom);
                check(near(bottom, 7000.0), "gap=0: cap levhasi tabani kolon ustunde (7000)");
                check(near(cap->center.z, 7005.0), "gap=0: merkez = 7000 + t/2");
            }
        }
    }

    // 3) gap = 25, t = 12: levha 25 mm yukarida.
    {
        ComponentInput in = makeInput();
        in.params.push_back(num(L"topPlateGap", 25.0));
        in.params.push_back(num(L"topPlateThickness", 12.0));
        const GenerationPlan plan =
            ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
        check(plan.ok, "gap=25: plan uretildi");
        if (plan.ok) {
            bool found = false;
            const PlateSpec* cap = findPlate(plan, "haunch_top_plate", &found);
            check(found, "gap=25: cap levhasi planda var");
            if (cap) {
                const double bottom = cap->center.z - cap->thickness * 0.5;
                std::printf("       gap=25: t=%.1f merkez_z=%.2f alt=%.2f\n",
                            cap->thickness, cap->center.z, bottom);
                check(near(cap->thickness, 12.0), "gap=25: cap kalinligi 12");
                check(near(bottom, 7025.0), "gap=25: cap levhasi tabani 7025 (kolon ustu + 25)");
                check(near(cap->center.z, 7031.0), "gap=25: merkez = 7000 + 25 + 12/2");
            }
            // 4) Alin levhasi cap boslugundan ETKILENMEZ (x=120 yuzu).
            bool epFound = false;
            const PlateSpec* ep = findPlate(plan, "haunch_end_plate", &epFound);
            check(epFound, "gap=25: alin levhasi planda var");
            if (ep) {
                const double front = ep->center.x - std::fabs(ep->axis.x) * ep->thickness * 0.5;
                std::printf("       gap=25: alin levhasi on yuz x=%.2f\n", front);
                check(near(front, 120.0, 1.0),
                      "gap=25: alin levhasi kolon yuzunde kalir (x=120)");
            }
        }
    }

    std::printf("%s\n", bad == 0 ? "TUM KONTROLLER GECTI" : "HATA VAR");
    return bad == 0 ? 0 : 1;
}
