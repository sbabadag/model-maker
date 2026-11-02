// MM-5 fix (item 4) — END PLATE KIRIS KESIMI testi (OCC GEREKTIRMEZ).
//
// Studio gozlemi: levha kolon yuzune oturdu (3 -> 0) ama kiris levha kalinligina
// gore KESILMIYORDU; aciklik kiris tarafina kaydi (fixture'da 3 mm). Cozum:
// generateEndPlate (ve recipe tasi) kirise PlaneCut uretir — kiris ucu levhanin
// KIRIS yuzu: duzlem = plateCenter + axis*t/2, normal = axis, keepPositive.
#include "model_maker/component_engine.hpp"
#include "model_maker/end_plate.hpp"
#include "model_maker/geometry.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

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
    b.secWidth = 200;
    b.secHeight = 400;
    b.secWebThickness = 8.6;
    b.secFlangeThickness = 13.5;
    in.secondary = {b};
    return in;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const ComponentInput in = makeInput();
    const GenerationPlan plan = generateEndPlate(in);
    check(plan.ok, "0 generateEndPlate ok");
    if (!plan.ok) {
        std::printf("  error: %ls\n", plan.error.c_str());
        std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
        return 1;
    }

    // 1) Tam olarak bir kiris kesimi var ve roleKey sabit.
    check(plan.cuts.size() == 1, "1 tek kesme uretildi");
    check(plan.cutKeys.size() == 1 && plan.cutKeys[0] == "cut_beam_end",
          "1 roleKey 'cut_beam_end'");
    if (plan.cuts.empty()) {
        std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
        return 1;
    }
    const CutModifier& cut = plan.cuts.front();

    // 2) Hedef KIRIS (kolon degil), duzlem kesimi, gizli.
    check(cut.kind == CutKind::PlaneCut, "2 kind = PlaneCut");
    check(cut.target == in.secondary.front().id, "2 hedef kiris uyesi");
    check(cut.hidden, "2 hidden (bilesen ic motor elemani)");
    check(cut.keepPositive, "2 korunan taraf kiris govdesi");

    // 3) Duzlem levhanin KIRIS yuzunde: origin = plateCenter + axis*t/2.
    check(plan.plates.size() == 1, "3 levha uretildi");
    const PlateSpec& plate = plan.plates.front();
    const Vec3 expected{plate.center.x + plate.axis.x * plate.thickness * 0.5,
                        plate.center.y + plate.axis.y * plate.thickness * 0.5,
                        plate.center.z + plate.axis.z * plate.thickness * 0.5};
    check(approx(cut.planeOrigin.x, expected.x) && approx(cut.planeOrigin.y, expected.y) &&
              approx(cut.planeOrigin.z, expected.z),
          "3 kesme duzlemi levhanin kiris yuzunde");
    check(approx(std::fabs(cut.planeNormal.x), std::fabs(plate.axis.x)) &&
              approx(std::fabs(cut.planeNormal.y), std::fabs(plate.axis.y)) &&
              approx(std::fabs(cut.planeNormal.z), std::fabs(plate.axis.z)),
          "3 kesme normali kiris eksenine paralel");

    // 4) Kesme duzlemi levha govdesini KESMEZ (kendi levhasini kesmemeli):
    //    levha merkezinden kiris yonunde t/2 uzakta -> levhanin disinda.
    const Vec3 toCenter{plate.center.x - cut.planeOrigin.x,
                        plate.center.y - cut.planeOrigin.y,
                        plate.center.z - cut.planeOrigin.z};
    const double along = toCenter.x * plate.axis.x + toCenter.y * plate.axis.y +
                         toCenter.z * plate.axis.z;
    check(approx(along, -plate.thickness * 0.5), "4 duzlem levha merkezinin t/2 ilerisinde");

    // 5) gap arttiginda kesme de levha ile birlikte kayar (aciklik levha tarafinda
    //    kalir, kiris tarafinda DEGIL).
    ComponentInput gapped = makeInput();
    gapped.params.push_back(CompParam{L"gap", CompParamValue{10.0}});
    const GenerationPlan pg = generateEndPlate(gapped);
    check(pg.ok, "5 gap=10 plan ok");
    if (pg.ok && !pg.cuts.empty() && !pg.plates.empty()) {
        const Vec3 e2{pg.plates[0].center.x + pg.plates[0].axis.x * pg.plates[0].thickness * 0.5,
                      pg.plates[0].center.y + pg.plates[0].axis.y * pg.plates[0].thickness * 0.5,
                      pg.plates[0].center.z + pg.plates[0].axis.z * pg.plates[0].thickness * 0.5};
        check(approx(pg.cuts[0].planeOrigin.x, e2.x) && approx(pg.cuts[0].planeOrigin.y, e2.y) &&
                  approx(pg.cuts[0].planeOrigin.z, e2.z),
              "5 gap arttiginda kesme levha kiris yuzunu izler");
        const double dx = pg.cuts[0].planeOrigin.x - cut.planeOrigin.x;
        const double dy = pg.cuts[0].planeOrigin.y - cut.planeOrigin.y;
        const double dz = pg.cuts[0].planeOrigin.z - cut.planeOrigin.z;
        check(approx(std::sqrt(dx * dx + dy * dy + dz * dz), 10.0, 1e-6),
              "5 kesme gap kadar kaydi (10 mm)");
    }

    std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
