// COMPONENT DEPENDENCY + RECOMPUTE (Phase R2d-2) — kabul testleri.
//
// 1. ComponentDependencyGraph: parca -> component ters indeksi (main + secondary),
//    dependents/dependentsOf (tekillestirme + artan id sirasi), dependsOn.
// 2. RecomputePlanner: yalnizca degisen parcaya bagli component'ler cozulur
//    (bagimsizlar dokunulmaz), deterministik sira, per-component durum,
//    allSucceeded toplamasi (herhangi bir basarisizlik -> false = atomik iptal).

#include "model_maker/component_recompute.hpp"

#include <cstdio>
#include <string>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++failures;                                                      \
            std::printf("FAIL: %s\n", msg);                                  \
        } else {                                                            \
            std::printf("PASS %s\n", msg);                                   \
        }                                                                    \
    } while (0)

namespace {
ComponentInstanceRecord makeComp(EntityId id, EntityId main, std::vector<EntityId> sec = {}) {
    ComponentInstanceRecord r;
    r.id = id;
    r.typeId = L"EndPlate";
    r.mainPart = main;
    r.secondaryParts = std::move(sec);
    return r;
}
} // namespace

int main() {
    // Uc component: C1(main=100, sec=200), C2(main=100, sec=201), C3(main=300).
    const std::vector<ComponentInstanceRecord> comps = {
        makeComp(11, 100, {200}), makeComp(22, 100, {201}), makeComp(33, 300)};

    // --- Bağımlılık grafiği: dependents ---
    {
        ComponentDependencyGraph g;
        g.build(comps);
        const auto d100 = g.dependents(100);
        CHECK(d100.size() == 2 && d100[0] == 11 && d100[1] == 22,
              "G1: parca 100 -> {11,22} (artan id)");
        const auto d300 = g.dependents(300);
        CHECK(d300.size() == 1 && d300[0] == 33, "G2: parca 300 -> {33}");
        CHECK(g.dependents(999).empty(), "G3: bilinmeyen parca -> bos");
        CHECK(g.dependents(200).size() == 1 && g.dependents(200)[0] == 11,
              "G4: secondary parca 200 -> {11}");
    }

    // --- dependsOn ---
    {
        CHECK(ComponentDependencyGraph::dependsOn(comps[0], 100), "G5: C1 main=100");
        CHECK(ComponentDependencyGraph::dependsOn(comps[0], 200), "G6: C1 secondary=200");
        CHECK(!ComponentDependencyGraph::dependsOn(comps[0], 300), "G7: C1 bagli degil 300");
        CHECK(!ComponentDependencyGraph::dependsOn(comps[2], 100), "G8: C3 bagli degil 100");
    }

    // --- dependentsOf: tekillestirme + sira ---
    {
        ComponentDependencyGraph g;
        g.build(comps);
        const auto d = g.dependentsOf({100, 300, 200});
        CHECK(d.size() == 3 && d[0] == 11 && d[1] == 22 && d[2] == 33,
              "G9: {100,300,200} -> {11,22,33} (tekillestirilmis + sirali)");
    }

    // --- RecomputePlanner: yalnizca etkilenenler + deterministik sira ---
    {
        RecomputePlanner planner;
        int calls = 0;
        RecomputePlanner::Resolver resolver = [&](const ComponentInstanceRecord& rec) {
            ++calls;
            RecomputeItem it;
            it.componentId = rec.id;
            it.status = RecomputeStatus::Success;
            it.origin = {static_cast<double>(rec.id), 0.0, 0.0};  // id -> x (test icin)
            return it;
        };
        // Degisen: parca 100 -> C1(11) ve C2(22) cozulur; C3(33) dokunulmaz.
        const auto plan = planner.plan(comps, {100}, resolver);
        CHECK(plan.allSucceeded, "P1: plan basarili");
        CHECK(plan.items.size() == 2 && calls == 2, "P2: yalnizca 2 bagli component cozuldu");
        CHECK(plan.items[0].componentId == 11 && plan.items[1].componentId == 22,
              "P3: deterministik sira {11,22}");
        CHECK(plan.items[0].origin.x == 11.0 && plan.items[1].origin.x == 22.0,
              "P4: yeni yerlesim dogru");
    }

    // --- RecomputePlanner: bagimsiz component dokunulmaz ---
    {
        RecomputePlanner planner;
        int calls = 0;
        const auto plan = planner.plan(comps, {300}, [&](const ComponentInstanceRecord& rec) {
            ++calls;
            RecomputeItem it;
            it.componentId = rec.id;
            it.status = RecomputeStatus::Success;
            return it;
        });
        CHECK(plan.items.size() == 1 && plan.items[0].componentId == 33 && calls == 1,
              "P5: yalnizca C3 (parca 300 baglisi) cozuldu");
    }

    // --- RecomputePlanner: hicbir parcaya bagli degil -> bos plan ---
    {
        RecomputePlanner planner;
        const auto plan = planner.plan(comps, {999}, [](const ComponentInstanceRecord& rec) {
            RecomputeItem it;
            it.componentId = rec.id;
            it.status = RecomputeStatus::Success;
            return it;
        });
        CHECK(plan.items.empty() && plan.allSucceeded, "P6: bilinmeyen parca -> bos plan");
    }

    // --- RecomputePlanner: basarisiz cozum -> allSucceeded=false (atomik iptal) ---
    {
        RecomputePlanner planner;
        RecomputePlanner::Resolver resolver = [](const ComponentInstanceRecord& rec) {
            RecomputeItem it;
            it.componentId = rec.id;
            // C2 (id=22) geometry failure; digerleri basarili.
            it.status = rec.id == 22 ? RecomputeStatus::GeometryFailure : RecomputeStatus::Success;
            it.error = rec.id == 22 ? L"geometri uretilemedi" : L"";
            return it;
        };
        const auto plan = planner.plan(comps, {100}, resolver);
        CHECK(!plan.allSucceeded, "P7: bir basarisizlik -> allSucceeded=false");
        CHECK(plan.items.size() == 2, "P8: her iki component de planda (durum raporlu)");
        CHECK(plan.items[0].status == RecomputeStatus::Success &&
                  plan.items[1].status == RecomputeStatus::GeometryFailure,
              "P9: per-component durum dogru");
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
