// mm_component_contract BAGIMSIZ smoke test — Qt ve OCCT olmadan derlenir/calisir.
// Kanitlar: (1) surum, (2) foundation (Vec3/WorkPlane), (3) contract engine
// (parametre semasi + dogrulama + hizalama), (4) dialog semasi dogrulama,
// (5) generator link + calisma.
#include "model_maker/component_engine.hpp"
#include "model_maker/component_dialog.hpp"
#include "model_maker/geometry.hpp"
#include "mm_contract/contract_version.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main() {
    // 1) Surum.
    CHECK(std::string(kContractVersion) == "1.5.0", "contract surumu 1.5.0");
    CHECK(contractSupports(1, 1), "contract 1.1 desteklenir");
    CHECK(contractSupports(1, 2), "contract 1.2 desteklenir");
    CHECK(contractSupports(1, 3), "contract 1.3 desteklenir");
    CHECK(contractSupports(1, 4), "contract 1.4 desteklenir");
    CHECK(contractSupports(1, 5), "contract 1.5 desteklenir (mevcut)");
    CHECK(contractSupports(1, 0), "contract 1.0 desteklenir");
    CHECK(!contractSupports(2, 0), "contract 2.0 desteklenmez (ust major)");
    CHECK(!contractSupports(0, 9), "contract 0.9 desteklenmez (alt major)");
    CHECK(!contractSupports(1, 6), "contract 1.6 desteklenmez (ust minor)");

    // 1b) Sabitler metinle eslesiyor mu (tutarlilik regresyonu)?
    // "major.minor.patch" string'ini ayrismayip dogrudan sayilarla karsilastir.
    {
        int ma = 0, mi = 0, pa = 0;
        const int n = std::sscanf(kContractVersion, "%d.%d.%d", &ma, &mi, &pa);
        CHECK(n == 3, "semver 3 parcadan olusur");
        CHECK(ma == kContractVersionMajor, "major sabiti metinle eslesir");
        CHECK(mi == kContractVersionMinor, "minor sabiti metinle eslesir");
        CHECK(pa == kContractVersionPatch, "patch sabiti metinle eslesir");
    }

    // 2) Foundation: Vec3 + WorkPlane.
    const Vec3 a{1.0, 2.0, 3.0};
    const Vec3 b{4.0, 5.0, 6.0};
    const Vec3 s = a + b;
    CHECK(s.x == 5.0 && s.y == 7.0 && s.z == 9.0, "Vec3 toplami");
    const WorkPlane w = WorkPlane::world();
    CHECK(w.origin.x == 0.0 && w.normal.z == 1.0, "WorkPlane::world (Z normali)");

    // 3) Contract engine: parametre semasi + dogrulama + hizalama.
    std::vector<CompParamSpec> specs;
    CompParamSpec len;
    len.name = L"haunchLength";
    len.type = CompParamType::Length;
    len.defDouble = 450.0;
    len.minDouble = 80.0;
    len.hasMinDouble = true;
    len.hasMaxDouble = true;
    len.maxDouble = 100000.0;
    specs.push_back(len);

    std::vector<CompParam> values;
    CompParam p;
    p.name = L"haunchLength";
    p.value = CompParamValue{1200.0};
    values.push_back(p);

    const auto aligned = alignParamsToSpecs(specs, values);
    CHECK(aligned.size() == 1, "hizalama tek parametre korur");
    CHECK(paramAsDouble(aligned, L"haunchLength", -1.0) == 1200.0, "hizalanan deger korunur");

    const auto clamped = validateParamValue(len, CompParamValue{10.0});
    CHECK(std::get<double>(clamped) == 80.0, "aralik disi deger min'e kelepcelenir");

    // 4) Dialog semasi dogrulama (typeId zorunlu; tek parametre).
    ComponentDialogSchema schema;
    schema.typeId = L"test_smoke";
    CHECK(validateDialogSchema(schema, specs).ok, "dialog semasi gecerli");

    // 5) Generator link + calisma kaniti (sonuc girdiye bagli; yalniz cagrilir).
    ComponentInput input;
    input.main.axisFrom = Vec3{0.0, 0.0, 0.0};
    input.main.axisTo = Vec3{0.0, 0.0, 3000.0};
    input.main.secWidth = 300.0;
    input.main.secHeight = 300.0;
    MemberGeometry beam;
    beam.axisFrom = Vec3{0.0, 0.0, 3000.0};
    beam.axisTo = Vec3{5000.0, 0.0, 3000.0};
    beam.secWidth = 150.0;
    beam.secHeight = 300.0;
    input.secondary.push_back(beam);
    const GenerationPlan plan = generateEndPlate(input);
    (void)plan; // link + calisma kaniti.

    // 6) memberSectionFrame (public, MM-4): profil donusunu kesit cercevesine uygular.
    {
        MemberGeometry m;
        m.axisFrom = Vec3{0.0, 0.0, 0.0};
        m.axisTo = Vec3{1000.0, 0.0, 0.0};   // +X eksen
        m.refNormal = Vec3{0.0, 0.0, 1.0};
        m.profileRotation = 0.0;
        const Vec3 axis{m.axisTo.x - m.axisFrom.x, m.axisTo.y - m.axisFrom.y,
                        m.axisTo.z - m.axisFrom.z};
        const ProfileSectionFrame f0 = memberSectionFrame(m);
        const ProfileSectionFrame ref = profileSectionFrame(axis, m.refNormal);
        auto close3 = [](const Vec3& a, const Vec3& b) {
            return std::fabs(a.x - b.x) < 1e-9 && std::fabs(a.y - b.y) < 1e-9 &&
                   std::fabs(a.z - b.z) < 1e-9;
        };
        CHECK(close3(f0.width, ref.width) && close3(f0.height, ref.height),
              "memberSectionFrame(0) == profileSectionFrame(axis, refNormal)");
        m.profileRotation = 90.0;
        const ProfileSectionFrame f90 = memberSectionFrame(m);
        CHECK(close3(f90.width, f0.height), "90°: genislik <- eski yukseklik");
        CHECK(close3(f90.height, Vec3{-f0.width.x, -f0.width.y, -f0.width.z}),
              "90°: yukseklik <- -eski genislik");
    }

    std::printf("contract smoke test: %s\n", failures ? "HATALI" : "OK");
    return failures ? 1 : 0;
}
