// MM-7 — BULON/UYE CAKISMA + EC3 MIN KENAR/ARALIK KONTROLU testi (OCC GEREKTIRMEZ).
//
// runComponentChecks'in dogru calistigini olcer:
//   1) bulon deligi (d0 + kaynak payi) kiris basligi/govdesiyle cakistiginda
//      pass=false + Turkce mesaj (hangi bulon, hangi uye, kac mm);
//   2) gecerli yerlesimde tum kontroller gecer;
//   3) kenar mesafesi (e1/e2 >= 1.2*d0) ve aralik (p1>=2.2*d0, p2>=2.4*d0);
//   4) kesit kalinliklari bilinmeyen uyede cakisma kontrolu atlanir;
//   5) codec round-trip (checks bos iken yazilmaz).
#include "model_maker/component_checks.hpp"
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/geometry.hpp"

#include <cstdio>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Kolon 300x300 (main) + kiris 200x400 (secondary_0, +X yonunde). tf/tw dolu.
static ComponentInput makeInput() {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = {0, 0, 1};
    in.main.secWidth = 300;
    in.main.secHeight = 300;
    in.main.secWebThickness = 11;
    in.main.secFlangeThickness = 19;

    MemberGeometry b;
    b.id = 2;
    b.axisFrom = {0, 0, 3000};
    b.axisTo = {2000, 0, 3000};
    b.refNormal = {0, 0, 1};
    b.secWidth = 200;    // flans genisligi
    b.secHeight = 400;   // kesit yuksekligi
    b.secWebThickness = 8.6;
    b.secFlangeThickness = 13.5;
    in.secondary.push_back(b);
    return in;
}

static ComponentInput withEdge(ComponentInput in, double edge) {
    in.params.push_back(CompParam{L"boltEdge", CompParamValue{edge}});
    return in;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    // 1) CAKISMA: kucuk boltEdge -> bulonlar basliga girer -> ureteci FAIL-CLOSED
    //    (plan.ok=false + checks[] dolu + Turkce mesaj).
    {
        const ComponentInput in = withEdge(makeInput(), 25.0);
        const GenerationPlan plan = generateEndPlate(in);
        check(!plan.ok, "1 plan fail-closed (cakisma -> ok=false)");
        check(!plan.error.empty(), "1 plan.error Turkce mesaj dolu");
        bool foundClash = false;
        for (const auto& c : plan.checks)
            if (!c.pass && (c.id == "bolt_flange_clash" || c.id == "bolt_web_clash")) {
                foundClash = true;
                check(!c.message.empty(), "1 ihlal mesaji Turkce dolu");
                check(c.value < c.limit, "1 value < limit (net mesafe yetersiz)");
            }
        check(foundClash, "1 baslik/govde cakismasi tespit edildi");
    }

    // 2) GECERLI: buyuk boltEdge -> bulonlar baslik disinda -> tum kontroller gecer.
    {
        const ComponentInput in = withEdge(makeInput(), 60.0);
        const GenerationPlan plan = generateEndPlate(in);
        check(plan.ok, "2 plan ok");
        const std::vector<ComponentCheck> cs = runComponentChecks(plan, in);
        check(allChecksPass(cs), "2 tum kontroller gecer (buyuk kenar)");
        // kenar + aralik kontrolleri uretildi mi.
        bool edge = false, p1 = false, p2 = false;
        for (const auto& c : cs) {
            if (c.id == "edge_distance") edge = true;
            if (c.id == "pitch_p1") p1 = true;
            if (c.id == "pitch_p2") p2 = true;
        }
        check(edge, "2 kenar mesafesi kontrolu var");
        check(p1, "2 p1 (dikey aralik) kontrolu var");
        check(p2, "2 p2 (yatay aralik) kontrolu var");
    }

    // 3) KENAR + ARALIK ihlali: elle kurulan plan (kucuk levha + sik bulon),
    //    ikincil uye YOK (yalniz kenar/aralik kontrolleri kosar).
    {
        GenerationPlan p;
        PlateSpec plate;
        plate.center = {0, 0, 0};
        plate.u = {1, 0, 0};
        plate.v = {0, 1, 0};
        plate.axis = {0, 0, 1};
        plate.width = 100.0;
        plate.height = 100.0;
        plate.thickness = 10.0;
        p.plates.push_back(plate);
        p.plateKeys.push_back("end_plate");
        BoltGroup b;
        b.origin = {0, -45, 0};  // alt bulon -v kenarina yakin (kenar ~5 mm)
        b.axis = {0, 0, 1};
        b.xDir = {1, 0, 0};
        b.countX = 2;
        b.countY = 1;
        b.spacingX = 10.0;   // p2 = 10 < 2.4*22 = 52.8
        b.spacingY = 0.0;
        b.params.holeDiameter = 22.0;
        p.boltGroups.push_back(b);
        p.boltGroupKeys.push_back("beam_bolts");
        ComponentInput empty;  // ikincil yok -> cakisma kontrolu uretilmez
        const std::vector<ComponentCheck> cs = runComponentChecks(p, empty);
        bool edgeFail = false, p2Fail = false;
        for (const auto& c : cs) {
            if (c.id == "edge_distance" && !c.pass) edgeFail = true;
            if (c.id == "pitch_p2" && !c.pass) p2Fail = true;
        }
        check(edgeFail, "3 kenar mesafesi ihlali yakalandi (5 < 1.2*d0)");
        check(p2Fail, "3 p2 ihlali yakalandi (10 < 2.4*d0)");
    }

    // 4) KESIT BILINMEYEN: tf/tw = 0 -> cakisma kontrolu atlanir (yanlis ret yok).
    {
        ComponentInput in = makeInput();
        in.secondary[0].secWebThickness = 0.0;
        in.secondary[0].secFlangeThickness = 0.0;
        const GenerationPlan plan = generateEndPlate(in);
        const std::vector<ComponentCheck> cs = runComponentChecks(plan, in);
        bool clashEmitted = false;
        for (const auto& c : cs)
            if (c.id == "bolt_flange_clash" || c.id == "bolt_web_clash") clashEmitted = true;
        check(!clashEmitted, "4 kesit bilinmiyorsa cakisma kontrolu atlanir");
    }

    // 5) CODEX round-trip: checks JSON'a yazilir (dolu) / bosken yazilmaz.
    {
        GenerationPlan p;
        p.ok = true;
        ComponentCheck c;
        c.id = "bolt_flange_clash";
        c.pass = false;
        c.value = 15.0;
        c.limit = 21.0;
        c.message = L"bulon (1,1) secondary_0 baslik izdusumuyle cakisiyor";
        p.checks.push_back(c);
        const nlohmann::json j = generationPlanToJson(p);
        check(j.contains("checks") && j["checks"].is_array(), "5 checks JSON'a yazildi");
        const GenerationPlan back = generationPlanFromJson(j);
        check(back.checks.size() == 1 && back.checks[0].id == "bolt_flange_clash" &&
                  back.checks[0].pass == false && !back.checks[0].message.empty(),
              "5 checks round-trip korunur");

        GenerationPlan empty;
        empty.ok = true;
        const nlohmann::json je = generationPlanToJson(empty);
        check(!je.contains("checks"), "5 bos checks yazilmaz");
        // Digest, checks icerse bile GEOMETRI uzerinden hesaplanir (checks cikarilir).
        const std::string d1 = generationPlanDigest(p);
        GenerationPlan p2 = p;
        p2.checks.clear();
        const std::string d2 = generationPlanDigest(p2);
        check(d1 == d2, "5 checks digest'i degistirmez (yalniz geometri)");
    }

    std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
