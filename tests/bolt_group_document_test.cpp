// BULON GRUBU DOKUMAN ENTEGRASYONU (Phase B1, Slice 3a) — OCC'siz core test.
//
// BoltGroup belge kaydi + delik (CutKind::Hole, owner) + display model +
// undo/redo + MMW8 round-trip + deleteModels kaskadi. windows.h yok; Linux'ta
// kosar.
//
// Semantik: addBoltGroup bir bulon kaydi + N delik (owner=id) + 1 display
// modeli ekler; removeBoltGroup hepsini siler; setBoltGroup delikleri yeniden
// uretir; deleteModels bagli parca silinince bulonu da kaldirir.

#include "model_maker/document.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

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
    Document doc;

    // Iki levha (duz dikdortgen, z=0 ve z=20 — grip eksen boyunca 20mm).
    doc.pushSnapshot();
    doc.addModel(WireframeModel::rectangle({0, 0, 0}, {100, 100, 0}));
    const EntityId plateA = doc.modelIds().back();
    doc.addModel(WireframeModel::rectangle({0, 0, 20}, {100, 100, 20}));
    const EntityId plateB = doc.modelIds().back();
    check(plateA != kInvalidEntityId && plateB != kInvalidEntityId,
          "iki levha eklendi");

    // Bulon grubu: M20/22, origin (50,50,10), eksen +Z.
    BoltGroup bg;
    bg.parts = {plateA, plateB};
    bg.origin = {50, 50, 10};
    bg.axis = {0, 0, 1};
    bg.params.nominalDiameter = 20.0;
    bg.params.holeDiameter = 22.0;
    bg.params.grade = "8.8";
    WireframeModel display = WireframeModel::cube(10.0);  // placeholder gorsel

    const std::size_t modelCountBefore = doc.models().size();

    doc.pushSnapshot();
    const std::size_t bi = doc.addBoltGroup(bg, display);
    check(bi == 0, "bulon grubu indeksi 0");
    check(doc.boltGroups().size() == 1, "1 bulon grubu kaydi");
    const EntityId boltId = doc.boltGroups()[0].id;
    check(boltId != kInvalidEntityId, "bulon id gecerli");
    check(doc.boltGroups()[0].displayEntityId != kInvalidEntityId,
          "display id atandi");
    // Display modeli eklendi mi?
    check(doc.indexOfEntity(doc.boltGroups()[0].displayEntityId).has_value(),
          "display modeli belgede");
    check(doc.models().size() == modelCountBefore + 1,
          "1 display modeli eklendi");
    // Delikler: 2 adet, owner == boltId, target = plateA/plateB.
    const auto holes = doc.boltGroupHoles(boltId);
    check(holes.size() == 2, "2 delik uretildi");
    bool targetsOk = true, ownerOk = true, kindOk = true, diamOk = true;
    for (const std::size_t ci : holes) {
        const auto& m = doc.cutModifiers()[ci];
        if (m.kind != CutKind::Hole) kindOk = false;
        if (m.owner != boltId) ownerOk = false;
        if (m.target != plateA && m.target != plateB) targetsOk = false;
        if (m.diameter != 22.0) diamOk = false;
    }
    check(kindOk && ownerOk && targetsOk && diamOk,
          "delikler Hole/owner/target/cap dogru");

    // grip: iki levha z=0..20 -> eksen (+Z) boyunca 20mm.
    check(std::abs(doc.boltGripLength(doc.boltGroups()[0]) - 20.0) < 1e-9,
          "grip uzunlugu 20mm");

    // setBoltGroup: origin degisince delikler yeniden uretilir (hala 2 adet,
    // yeni origin).
    doc.pushSnapshot();
    BoltGroup moved = doc.boltGroups()[0];
    moved.origin = {80, 80, 10};
    doc.setBoltGroup(0, moved);
    check(doc.boltGroups()[0].origin == (Vec3{80, 80, 10}), "origin guncellendi");
    check(doc.boltGroupHoles(boltId).size() == 2, "tasima sonrasi hala 2 delik");
    {
        bool originOk = true;
        for (const std::size_t ci : doc.boltGroupHoles(boltId)) {
            if (doc.cutModifiers()[ci].planeOrigin != (Vec3{80, 80, 10}))
                originOk = false;
        }
        check(originOk, "delik merkezleri yeni origin'e tasindi");
    }

    // Undo: origin geri gelir; redo: tekrar gider.
    doc.undo();
    check(doc.boltGroups()[0].origin == (Vec3{50, 50, 10}), "undo origin geri");
    doc.redo();
    check(doc.boltGroups()[0].origin == (Vec3{80, 80, 10}), "redo origin yeni");

    // Undo bulon eklemeyi geri alir: kayit + delikler + display silinir.
    doc.undo();  // setBoltGroup'u geri al (origin 50,50,10)
    doc.undo();  // addBoltGroup'u geri al
    check(doc.boltGroups().empty(), "undo sonrasi bulon kaydi yok");
    check(doc.boltGroupHoles(boltId).empty(), "undo sonrasi delik yok");
    check(doc.models().size() == modelCountBefore, "undo sonrasi display silindi");
    // Levhalar korundu.
    check(doc.indexOfEntity(plateA).has_value() && doc.indexOfEntity(plateB).has_value(),
          "undo sonrasi levhalar korundu");

    // Redo bulonu geri getirir.
    doc.redo();  // addBoltGroup
    doc.redo();  // setBoltGroup
    check(doc.boltGroups().size() == 1, "redo sonrasi bulon geri geldi");

    // removeBoltGroup: kayit + delikler + display kaldirilir; levhalar kalir.
    doc.pushSnapshot();
    doc.removeBoltGroup(0);
    check(doc.boltGroups().empty(), "removeBoltGroup kaydi sildi");
    check(doc.boltGroupHoles(boltId).empty(), "removeBoltGroup delikleri sildi");
    check(doc.models().size() == modelCountBefore, "removeBoltGroup display sildi");
    check(doc.indexOfEntity(plateA).has_value() && doc.indexOfEntity(plateB).has_value(),
          "removeBoltGroup levhalari korudu");
    doc.undo();
    check(doc.boltGroups().size() == 1, "removeBoltGroup undo geri getirdi");

    // MMW8 round-trip: kaydet -> ac -> bulon + delikler + display korunur.
    {
        const auto path = std::filesystem::temp_directory_path() / "bolt_grp_doc.mmw8";
        doc.save(path);
        Document loaded;
        loaded.load(path);
        check(loaded.boltGroups().size() == 1, "MMW8: bulon kaydi round-trip");
        const EntityId lid = loaded.boltGroups()[0].id;
        check(lid == boltId, "MMW8: bulon id korundu");
        check(loaded.boltGroups()[0].displayEntityId == doc.boltGroups()[0].displayEntityId,
              "MMW8: display id korundu");
        check(loaded.boltGroupHoles(lid).size() == 2, "MMW8: delikler round-trip");
        check(loaded.boltGroups()[0].params.grade == "8.8" &&
                  loaded.boltGroups()[0].params.holeDiameter == 22.0,
              "MMW8: parametreler round-trip");
        check(std::abs(loaded.boltGripLength(loaded.boltGroups()[0]) - 20.0) < 1e-9,
              "MMW8: grip round-trip");
        std::filesystem::remove(path);
    }

    // deleteModels kaskadi: bagli parca silinince bulon da kaldirilir.
    {
        Document doc2;
        doc2.pushSnapshot();
        doc2.addModel(WireframeModel::rectangle({0, 0, 0}, {100, 100, 0}));
        const EntityId pa = doc2.modelIds().back();
        doc2.addModel(WireframeModel::rectangle({0, 0, 10}, {100, 100, 10}));
        const EntityId pb = doc2.modelIds().back();
        BoltGroup bg2;
        bg2.parts = {pa, pb};
        bg2.origin = {50, 50, 10};
        bg2.axis = {0, 0, 1};
        bg2.params.nominalDiameter = 20.0;
        bg2.params.holeDiameter = 22.0;
        doc2.pushSnapshot();
        doc2.addBoltGroup(bg2, WireframeModel::cube(10.0));
        check(doc2.boltGroups().size() == 1, "kaskad: bulon eklendi");
        // plateA'yi sil -> bulon grubu kaldirilmali.
        doc2.pushSnapshot();
        doc2.deleteModels({*doc2.indexOfEntity(pa)});
        check(doc2.boltGroups().empty(), "kaskad: parca silinince bulon kaldirildi");
        check(doc2.cutModifiers().empty(), "kaskad: delik kalmadi");
        // Undo parca silmeyi geri alir -> bulon da geri gelir.
        doc2.undo();
        check(doc2.boltGroups().size() == 1, "kaskad undo: bulon geri geldi");
        check(doc2.indexOfEntity(pa).has_value(), "kaskad undo: parca geri geldi");
    }

    if (g_failures) {
        std::printf("%d/%d FAIL\n", g_failures, g_checks);
        return 1;
    }
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
}
