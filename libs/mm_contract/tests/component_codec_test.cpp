// MM-2 — component codec testi (standalone contract test).
//
// 1) UTF-8 roundtrip (Türkçe karakterler, wchar_t 16/32-bit).
// 2) CompParamSpec / CompParam (tüm variant'lar) roundtrip.
// 3) Yerleşik 4 tipin specs + dialog tanımları: encode -> decode -> re-encode
//    kanonik eşit (kayıpsız).
// 4) generateHaunch40 (5 sabit girdi): plan -> JSON -> plan kayıpsız.
// 5) planDigest: kanonik JSON üzerinden SHA-256; golden dosyasıyla doğrulanır
//    (yoksa üretilir — tests/golden/haunch40_digests.json).

#include "model_maker/component_codec.hpp"
#include "model_maker/component_dialog.hpp"
#include "model_maker/component_engine.hpp"
#include "mm_contract/contract_version.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace mm;
using json = nlohmann::json;

static int bad = 0;
static void check(bool p, const char* what) {
    std::printf("%s %s\n", p ? "PASS" : "FAIL", what);
    bad += !p;
}

// M_PI mingw'de tanimli degildir; tasinabilir sabit.
static constexpr double kPi = 3.14159265358979323846;

static bool canonEq(const json& a, const json& b) {
    return canonicalJsonString(a) == canonicalJsonString(b);
}

// --- haunch40 girdi üreticileri (tests/haunch_inclined_test.cpp ile aynı) ---

static ComponentInput makeInput(double slopeDeg) {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = {0, 1, 0};
    in.main.secHeight = 300;
    in.main.secWidth = 300;
    in.main.secWebThickness = 11;
    in.main.secFlangeThickness = 19;
    in.main.sectionCenterOffset = 0.0;

    MemberGeometry beam;
    beam.id = 2;
    const double t = std::tan(slopeDeg * kPi / 180.0);
    beam.axisFrom = {0, 1200, 1500 + 1140 * t};
    beam.axisTo = {0, 60, 1500};
    beam.refNormal = {0, 0, 1};
    beam.secHeight = 200;
    beam.secWidth = 100;
    beam.secWebThickness = 5.6;
    beam.secFlangeThickness = 8.5;
    beam.sectionCenterOffset = 0.0;
    in.secondary = {beam};
    return in;
}

// 90° dönük kolon + eğik kiriş (portal_1.mmw deseni).
static ComponentInput makeInputRotatedColumn() {
    ComponentInput in;
    in.main.id = 45;
    in.main.axisFrom = {-115.0, 12000.0, 0.0};
    in.main.axisTo = {-115.0, 12000.0, 5000.0};
    in.main.refNormal = {0, 0, 1};
    in.main.secWidth = 240.0;
    in.main.secHeight = 230.0;
    in.main.secWebThickness = 7.5;
    in.main.secFlangeThickness = 12.0;
    in.main.sectionCenterOffset = -115.0;
    in.main.profileRotation = 90.0;
    MemberGeometry beam;
    beam.id = 47;
    beam.axisFrom = {0.0, 12000.0, 5000.0};
    beam.axisTo = {5997.780613671378, 12000.0, 5986.683682028266};
    beam.refNormal = {0, 0, 1};
    beam.secWidth = 180.0;
    beam.secHeight = 400.0;
    beam.secWebThickness = 8.6;
    beam.secFlangeThickness = 13.5;
    beam.sectionCenterOffset = 0.0;
    beam.profileRotation = 0.0;
    in.secondary = {beam};
    return in;
}

// Eaves (kiriş kolon tepesinde) + elle verilmiş, kolon tepesini AŞAN
// endPlateHeight (600 mm; kırpma yok çünkü açık değer verildi).
static ComponentInput makeInputManualEndPlateHeight() {
    ComponentInput in = makeInput(0.0);
    in.secondary[0].axisFrom = {0, 1200, 3000};
    in.secondary[0].axisTo = {0, 60, 3000};
    in.params = {CompParam{L"endPlateHeight", 600.0}};
    return in;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    std::printf("=== contract sürümü ===\n");
    check(std::string(kContractVersion) == "1.2.0", "contract sürümü 1.2.0");

    std::printf("=== UTF-8 roundtrip ===\n");
    {
        const std::wstring turkce = L"şğüöçİ ĞÜÖÇ Haunch 40 — eğik kiriş";
        const std::string utf8 = utf8FromWide(turkce);
        check(wideFromUtf8(utf8) == turkce, "wstring -> UTF-8 -> wstring birebir");
    }

    std::printf("=== CompParamSpec roundtrip ===\n");
    {
        std::vector<CompParamSpec> specs;
        CompParamSpec s;
        s.name = L"uzunluk";
        s.type = CompParamType::Length;
        s.unit = L"mm";
        s.description = L"plaka boyu";
        s.defDouble = 450.0; s.minDouble = 80.0; s.maxDouble = 2000.0;
        s.hasMinDouble = true; s.hasMaxDouble = true;
        specs.push_back(s);

        s = CompParamSpec{};
        s.name = L"grade";
        s.type = CompParamType::Enum;
        s.defString = L"8.8";
        s.choices = {L"8.8", L"10.9"};
        specs.push_back(s);

        s = CompParamSpec{};
        s.name = L"adet";
        s.type = CompParamType::Integer;
        s.defInt = 2; s.minInt = 1; s.maxInt = 4;
        s.hasMinInt = true; s.hasMaxInt = true;
        specs.push_back(s);

        s = CompParamSpec{};
        s.name = L"aktif";
        s.type = CompParamType::Boolean;
        s.defBool = true;
        specs.push_back(s);

        s = CompParamSpec{};
        s.name = L"malzeme";
        s.type = CompParamType::Material;
        s.defString = L"S235JR";
        specs.push_back(s);

        s = CompParamSpec{};
        s.name = L"hedef";
        s.type = CompParamType::EntityRef;
        specs.push_back(s);

        for (std::size_t i = 0; i < specs.size(); ++i) {
            const json j = compParamSpecToJson(specs[i]);
            const CompParamSpec back = compParamSpecFromJson(j);
            check(canonEq(j, compParamSpecToJson(back)), "CompParamSpec roundtrip kayıpsız");
        }
    }

    std::printf("=== CompParam (variant) roundtrip ===\n");
    {
        const std::vector<CompParamValue> vals = {
            CompParamValue{450.5}, CompParamValue{2}, CompParamValue{true},
            CompParamValue{std::wstring(L"8.8")}, CompParamValue{EntityId(77)}};
        for (const auto& v : vals) {
            const CompParam p{L"x", v};
            const json j = compParamToJson(p);
            const CompParam back = compParamFromJson(j);
            check(canonEq(j, compParamToJson(back)), "CompParam variant roundtrip kayıpsız");
        }
    }

    std::printf("=== yerleşik 4 tip: ComponentType roundtrip ===\n");
    {
        const std::wstring ids[4] = {
            endPlateComponentType().typeId, finPlateComponentType().typeId,
            shearPlate146ComponentType().typeId, haunch40ComponentType().typeId};
        const ComponentType types[4] = {
            endPlateComponentType(), finPlateComponentType(),
            shearPlate146ComponentType(), haunch40ComponentType()};
        for (int i = 0; i < 4; ++i) {
            const json j = componentTypeToJson(types[i]);
            const ComponentType back = componentTypeFromJson(j);
            check(canonEq(j, componentTypeToJson(back)), "ComponentType roundtrip kayıpsız");
            check(back.typeId == ids[i] && !back.params.empty(),
                  "ComponentType typeId + params korundu");
        }
    }

    std::printf("=== yerleşik 4 tip: dialog şeması roundtrip ===\n");
    {
        const ComponentDialogDefinition defs[4] = {
            endPlateDialogDefinition(), finPlateDialogDefinition(),
            shearPlate146DialogDefinition(), haunch40DialogDefinition()};
        for (int i = 0; i < 4; ++i) {
            const json j = componentDialogSchemaToJson(defs[i].schema);
            const ComponentDialogSchema back = componentDialogSchemaFromJson(j);
            check(canonEq(j, componentDialogSchemaToJson(back)), "dialog şeması roundtrip kayıpsız");
        }
    }

    std::printf("=== generateHaunch40: 5 sabit girdi (roundtrip + digest) ===\n");
    struct Case {
        const char* name;
        ComponentInput input;
    };
    const Case cases[5] = {
        {"haunch40_horizontal", makeInput(0.0)},
        {"haunch40_inclined_5deg", makeInput(5.0)},
        {"haunch40_inclined_10deg", makeInput(10.0)},
        {"haunch40_rotated_column_90", makeInputRotatedColumn()},
        {"haunch40_manual_endplate_height", makeInputManualEndPlateHeight()},
    };

    json golden = json::object();
    for (const auto& c : cases) {
        GenerationPlan plan =
            ComponentRegistry::instance().generate(L"tekla_haunch_40", c.input);
        check(plan.ok, "plan üretildi (ok=true)");
        const json j = generationPlanToJson(plan);
        const GenerationPlan back = generationPlanFromJson(j);
        check(canonEq(j, generationPlanToJson(back)), "GenerationPlan roundtrip kayıpsız");

        const std::string digest = generationPlanDigest(plan);
        check(digest.size() == 64, "planDigest 64 hex karakter");
        std::printf("  %-34s %s\n", c.name, digest.c_str());
        golden[c.name] = digest;
    }

    std::printf("=== golden dosyası ===\n");
    const std::string goldenPath =
        std::string(MM_CONTRACT_GOLDEN_DIR) + "/haunch40_digests.json";
    {
        std::ifstream in(goldenPath);
        if (in.good()) {
            json onDisk;
            in >> onDisk;
            in.close();
            check(onDisk.is_object() && onDisk.size() == 5, "golden: 5 girdi mevcut");
            for (const auto& c : cases) {
                if (onDisk.contains(c.name)) {
                    check(onDisk[c.name].get<std::string>() == golden[c.name].get<std::string>(),
                          "golden digest birebir eşleşiyor");
                } else {
                    check(false, "golden girdisi eksik");
                }
            }
        } else {
            std::ofstream out(goldenPath);
            out << golden.dump(2) << "\n";
            out.close();
            std::printf("  golden üretildi: %s\n", goldenPath.c_str());
        }
    }

    std::printf("\n%s (%d hata)\n", bad ? "BASARISIZ" : "TUM TESTLER GECTI", bad);
    return bad ? 1 : 0;
}
