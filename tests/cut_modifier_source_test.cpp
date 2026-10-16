// KESME KAYNAK VARLIGI (P6) — cekirdek test.
//
// CutModifier.source: PlaneCut/Fitting/LineCut/PolygonCut kesmesi kaynak
// varligi (konstrüksiyon duzlemi/cizgi/3DFACE) REFERANS alir — geometri
// KOPYALANMAZ. Bu test kilitler:
//   - resolveCutModifierSource: kaynagin GUNCEL geometrisi mod'a yansir,
//     kaynak tasininca origin/normal/cokgen izler.
//   - deleteModels kaskadi: kaynak silinince onu kullanan modifier kaldirilir
//     ve etkilenen hedef (recompute icin) doner.
//   - source=kInvalidEntityId (eski/kopya davranis) kaskad tetiklemez.
// OCC'siz, Linux'ta kosar.

#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool near3(const Vec3& a, const Vec3& b, double tol = 1e-9) {
    return std::abs(a.x - b.x) < tol && std::abs(a.y - b.y) < tol && std::abs(a.z - b.z) < tol;
}

int main() {
    // --- 1. Kaynak cozumu: duzlem tasininca modifier geometrisi izler ---
    {
        Document doc;
        doc.addModel(WireframeModel::line({0, 0, 0}, {1000, 0, 0}));
        const EntityId targetId = doc.modelId(0);
        const std::size_t planeIdx = doc.addConstructionPlane(WorkPlane::world(), 1000.0);
        const EntityId planeId = doc.modelId(planeIdx);

        CutModifier m;
        m.kind = CutKind::PlaneCut;
        m.target = targetId;
        m.source = planeId;
        m.planeOrigin = {999, 999, 999};  // eski/kopya deger — cozum EZMELI
        m.planeNormal = {1, 1, 1};
        doc.addCutModifier(m);

        auto resolved = resolveCutModifierSource(doc.cutModifiers()[0], doc);
        check(near3(resolved.planeOrigin, {0, 0, 0}), "kaynak duzlem origin (baslangic)");
        check(near3(resolved.planeNormal, {0, 0, 1}), "kaynak duzlem normal (baslangic)");

        // Duzlemi +Z yonunde 500 mm tasi.
        doc.moveModels({planeIdx}, {0, 0, 500});
        resolved = resolveCutModifierSource(doc.cutModifiers()[0], doc);
        check(near3(resolved.planeOrigin, {0, 0, 500}), "duzlem tasininca origin izler");
        check(near3(resolved.planeNormal, {0, 0, 1}), "normal korunur");
    }

    // --- 2. Kaynak silinince kaskad: modifier kaldirilir + hedef doner ---
    {
        Document doc;
        doc.addModel(WireframeModel::line({0, 0, 0}, {1000, 0, 0}));
        const EntityId targetId = doc.modelId(0);
        const std::size_t planeIdx = doc.addConstructionPlane(WorkPlane::world(), 1000.0);
        const EntityId planeId = doc.modelId(planeIdx);
        CutModifier m;
        m.kind = CutKind::Fitting;
        m.target = targetId;
        m.source = planeId;
        doc.addCutModifier(m);
        check(doc.cutModifiers().size() == 1, "kaskad oncesi 1 modifier");
        const auto affected = doc.deleteModels({planeIdx});
        check(doc.cutModifiers().empty(), "kaynak silinince modifier kaskad kaldirilir");
        check(affected.size() == 1 && affected[0] == targetId, "etkilenen hedef doner");
    }

    // --- 3. source=kInvalidEntityId (eski dosya) kaskad tetiklemez ---
    {
        Document doc;
        doc.addModel(WireframeModel::line({0, 0, 0}, {1000, 0, 0}));
        const EntityId targetId = doc.modelId(0);
        const std::size_t planeIdx = doc.addConstructionPlane(WorkPlane::world(), 1000.0);
        CutModifier m;
        m.kind = CutKind::PlaneCut;
        m.target = targetId;
        m.source = kInvalidEntityId;  // kopya davranis
        m.planeOrigin = {0, 0, 0};
        m.planeNormal = {0, 0, 1};
        doc.addCutModifier(m);
        const auto affected = doc.deleteModels({planeIdx});
        check(doc.cutModifiers().size() == 1, "source=0 kaskad tetiklemez");
        check(affected.empty(), "kaynak yoksa hedef donmez");
    }

    // --- 4. cutModifiersForSource ters sorgusu ---
    {
        Document doc;
        doc.addModel(WireframeModel::line({0, 0, 0}, {1000, 0, 0}));
        const EntityId targetId = doc.modelId(0);
        const std::size_t planeIdx = doc.addConstructionPlane(WorkPlane::world(), 1000.0);
        const EntityId planeId = doc.modelId(planeIdx);
        CutModifier m;
        m.kind = CutKind::LineCut;
        m.target = targetId;
        m.source = planeId;
        doc.addCutModifier(m);
        const auto idxs = doc.cutModifiersForSource(planeId);
        check(idxs.size() == 1 && idxs[0] == 0, "cutModifiersForSource ters sorgu");
        check(doc.cutModifiersForSource(kInvalidEntityId).empty(), "source=0 sorgusu bos");
    }

    // --- 5. P8: kaynak-bagli modifier isabet testi (includeSourceBound) ---
    // Alt+sol tik (yon tersi) kaynak-bagli modifier'i BULMALI; normal secim/hover
    // yolu (false) ise kaynak nesnenin secilmesi icin onu ATLAMALI.
    {
        Document doc;
        doc.addModel(WireframeModel::line({-100, -100, 0}, {100, 100, 0}));
        const EntityId targetId = doc.modelId(0);
        const std::size_t planeIdx = doc.addConstructionPlane(WorkPlane::world(), 1000.0);
        const EntityId planeId = doc.modelId(planeIdx);
        CutModifier m;
        m.kind = CutKind::PlaneCut;
        m.target = targetId;
        m.source = planeId;
        doc.addCutModifier(m);

        const auto skipped = hitTestCutModifier2D({0, 0, 0}, doc, 10.0);
        check(!skipped, "kaynak-bagli modifier secim/hover yolunda atlanir");
        const auto found = hitTestCutModifier2D({0, 0, 0}, doc, 10.0, true);
        check(found && *found == 0, "kaynak-bagli modifier Alt+tik (true) yolunda bulunur");
    }

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
