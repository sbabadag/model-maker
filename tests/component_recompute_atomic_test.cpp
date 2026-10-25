// COMPONENT RECOMPUTE ATOMİKLİĞİ + GENERIC ADAPTER (Slice 2d-2c P0/P1).
//
// Iki sey dogrulanir (OCC'siz, saf core):
//   (1) ATOMIKLIK: atomicRecompute prepare-then-commit — bir prepare bile
//       basarisizsa HICBIR commit calismaz (kismi belge degisikligi YOK).
//       Hata enjeksiyonu ile (prepare(conn2) -> nullopt).
//   (2) GENERIC: ComponentRefs adapter'i ile EndPlateConnection VE
//       ComponentInstanceRecord AYNI grafik/planlayici hattini kullanir —
//       Document/Application icinde component tipine ozel recompute yolu yok.
//
// windows.h yok; Linux'ta kosar.

#include "model_maker/component_recompute.hpp"

#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <vector>

using namespace mm;

static int g_checks = 0;
static int g_failures = 0;
static void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL %s\n", what);
    }
}

int main() {
    // --- (1) GENERIC GRAPH: EndPlateConnection adapter ---
    // Ayni kolona (part=10) iki End Plate (id 2 ve 5), farkli kirislere (11, 12).
    std::vector<EndPlateConnection> conns;
    {
        EndPlateConnection a;
        a.id = 2; a.mainPart = 10; a.secondaryPart = 11;
        EndPlateConnection b;
        b.id = 5; b.mainPart = 10; b.secondaryPart = 12;
        conns = {a, b};
    }
    std::vector<ComponentRefs> refs;
    for (const auto& c : conns) refs.push_back(componentRefs(c));
    check(refs.size() == 2, "2 EndPlateConnection -> 2 ComponentRefs");
    check(refs[0].id == 2 && refs[0].parts.size() == 2, "refs[0] id+2 parca");
    check(refs[1].id == 5 && refs[1].parts.size() == 2, "refs[1] id+2 parca");

    ComponentDependencyGraph g;
    g.build(refs);
    const std::vector<EntityId> dep = g.dependentsOf({10});
    check(dep.size() == 2 && dep[0] == 2 && dep[1] == 5,
          "kolon(10) -> iki baglanti, ARTAN id (2,5) deterministik");

    // Kiris 12 hareket ederse yalnizca id=5 etkilenir.
    const std::vector<EntityId> dep12 = g.dependentsOf({12});
    check(dep12.size() == 1 && dep12[0] == 5, "kiris(12) -> yalnizca baglanti 5");

    // --- (2) GENERIC: farkli component tipleri ayni graf/planner ---
    // EndPlateConnection + ComponentInstanceRecord karisik; ayni planlama hatti.
    ComponentInstanceRecord base;
    base.id = 7; base.mainPart = 10; base.secondaryParts = {13};
    std::vector<ComponentRefs> mixed = {componentRefs(conns[0]), componentRefs(conns[1]),
                                        componentRefs(base)};
    ComponentDependencyGraph mg;
    mg.build(mixed);
    const std::vector<EntityId> mdep = mg.dependentsOf({10});
    check(mdep.size() == 3 && mdep[0] == 2 && mdep[1] == 5 && mdep[2] == 7,
          "karisik tipler (EndPlate+Component) ayni graf, artan id (2,5,7)");

    // planner (planRefs) ayri tipleri kimlik-anahtarli resolver ile cozer.
    RecomputePlanner planner;
    RecomputePlanner::ResolverById resolver = [&](EntityId cid) {
        RecomputeItem it;
        it.componentId = cid;
        it.status = RecomputeStatus::Success;
        return it;
    };
    const auto plan = planner.planRefs(mixed, {10}, resolver);
    check(plan.allSucceeded && plan.items.size() == 3,
          "planRefs 3 component'i cozer (karisik tipler)");
    bool orderOk = plan.items[0].componentId == 2 && plan.items[1].componentId == 5 &&
                   plan.items[2].componentId == 7;
    check(orderOk, "planRefs deterministik sira (2,5,7)");

    // --- (3) ATOMIK RECOMPUTE: prepare-then-commit, hata enjeksiyonu ---
    struct Prepared { int tag; };
    std::vector<EntityId> commitCalls;
    std::vector<EntityId> prepareCalls;

    // 3a. Hicbir prepare basarisiz degil -> HEPSI commit, artan sira.
    commitCalls.clear();
    prepareCalls.clear();
    auto okPrepare = [&](EntityId id) -> std::optional<Prepared> {
        prepareCalls.push_back(id);
        return Prepared{static_cast<int>(id)};
    };
    auto commit = [&](EntityId id, const Prepared& p) -> bool {
        commitCalls.push_back(id);
        (void)p;
        return true;
    };
    auto r1 = atomicRecompute<Prepared>({5, 2, 7}, okPrepare, commit);
    check(r1.allPrepared, "3a: tum prepare basarili");
    check(r1.failed.empty(), "3a: failed bos");
    check(r1.committed == (std::vector<EntityId>{2, 5, 7}),
          "3a: commit ARTAN sira (2,5,7) — giris sirasiz verildi");
    check(commitCalls == (std::vector<EntityId>{2, 5, 7}), "3a: commit cagri sirasi artan");

    // 3b. FAY ENJEKSIYONU: prepare(5) -> nullopt. commit(2) CALISMAMALI.
    commitCalls.clear();
    prepareCalls.clear();
    auto failPrepare = [&](EntityId id) -> std::optional<Prepared> {
        prepareCalls.push_back(id);
        if (id == 5) return std::nullopt;  // 2. baglantinin B-Rep uretimi basarisiz
        return Prepared{static_cast<int>(id)};
    };
    auto r2 = atomicRecompute<Prepared>({2, 5}, failPrepare, commit);
    check(!r2.allPrepared, "3b: allPrepared=false (bir prepare basarisiz)");
    check(r2.failed == (std::vector<EntityId>{5}), "3b: failed={5}");
    check(r2.committed.empty(), "3b: HICBIR commit yok (kismi uygulama YOK)");
    check(commitCalls.empty(), "3b: commit() HIC calismadi — baglanti 2 uygulanmadi");
    check(prepareCalls == (std::vector<EntityId>{2, 5}), "3b: prepare ikisi icin de denendi");

    // 3c. BAGLANTI 2 BASARISIZKEN BAGLANTI 1 BAŞARILI — P0 senaryosu:
    //      1. baglanti prepare OK, 2. baglanti prepare FAIL -> 1. baglanti
    //      document'a UYGULANMAMALI (eski geometri korunur).
    commitCalls.clear();
    auto failSecond = [&](EntityId id) -> std::optional<Prepared> {
        if (id == 5) return std::nullopt;
        return Prepared{static_cast<int>(id)};
    };
    auto r3 = atomicRecompute<Prepared>({2, 5}, failSecond, commit);
    check(!r3.allPrepared && commitCalls.empty(),
          "3c: 1.baglanti OK + 2.baglanti FAIL -> 1.baglanti UYGULANMADI");

    // 3d. BASARISIZLIKTAN SONRA YENIDEN BASARILI recompute (retry).
    commitCalls.clear();
    auto r4 = atomicRecompute<Prepared>({2, 5}, okPrepare, commit);
    check(r4.allPrepared && r4.committed == (std::vector<EntityId>{2, 5}),
          "3d: hatadan sonra tekrar basarili recompute (2,5)");

    // 3e. COMMIT BASARISIZ (commit false doner) -> commitFailed + kismi commit.
    commitCalls.clear();
    auto failCommit = [&](EntityId id, const Prepared& p) -> bool {
        (void)p;
        if (id == 5) return false;  // 2. baglanti uygulanamadi (commit reddedildi)
        commitCalls.push_back(id);
        return true;
    };
    auto r5 = atomicRecompute<Prepared>({2, 5}, okPrepare, failCommit);
    check(r5.allPrepared && r5.commitFailed, "3e: prepare OK ama commit basarisiz");
    check(r5.committed == (std::vector<EntityId>{2}),
          "3e: yalnizca ilk baglanti commit edildi (kismi) — cagiran rollback etmeli");
    check(commitCalls == (std::vector<EntityId>{2}), "3e: commit(2) calisti, commit(5) reddedildi");

    // --- (4) planner status yayilimi: bir component unresolved -> allSucceeded false ---
    RecomputePlanner::ResolverById mixedResolver = [&](EntityId cid) {
        RecomputeItem it;
        it.componentId = cid;
        it.status = (cid == 5) ? RecomputeStatus::GeometryFailure : RecomputeStatus::Success;
        if (cid == 5) it.error = L"zorlanmis geometri hatasi";
        return it;
    };
    const auto plan2 = planner.planRefs(mixed, {10}, mixedResolver);
    check(!plan2.allSucceeded && plan2.items.size() == 3,
          "4: bir component GeometryFailure -> allSucceeded=false (cagiran uygulamaz)");

    std::printf("component_recompute_atomic: %d kontrol, %d basarisiz\n",
                g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
