// KOPYA: component'ler COMPONENT olarak kopyalanir (C3).
// Kullanici geri bildirimi (2026-10-28): "elemanlar kopyalaniyor ama component
// olarak kopyalanmiyor" — kopya, sahipsiz model yigini olarak kaliyordu.
// Kural: kopyalanan modellerin sahip oldugu component instance'i + bulon grubu
// KLONLANIR; kimlikler kopyalara eslenir, cerceve/yerlesim otelemeyle tasinir,
// tek undo adimi; YARIM kopyada (ana parca kopyalanmadiysa) instance ACILMAZ.
#include "model_maker/document.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static WireframeModel rect(double x, double y, double z) {
    return WireframeModel::rectangle({x, y, z}, {x + 100.0, y + 100.0, z});
}

// Ana parca (kiris) + ikincil (kolon) + component'in sahip oldugu levha + bulon grubu.
struct Fixture {
    Document doc;
    EntityId beam{}, column{}, plate{}, bolt{}, boltDisplay{};
};

static Fixture makeJoint(Document& doc) {
    Fixture f;
    f.doc = std::move(doc);
    f.doc.pushSnapshot();
    f.doc.addModel(rect(0, 0, 0));
    f.beam = f.doc.modelIds().back();
    f.doc.addModel(rect(0, 200, 0));
    f.column = f.doc.modelIds().back();
    f.doc.addModel(rect(50, 100, 0));
    f.plate = f.doc.modelIds().back();

    BoltGroup bg;
    bg.parts = {f.beam, f.plate};
    bg.origin = {100, 100, 0};
    bg.axis = {0, 0, 1};
    bg.countX = 2;
    bg.countY = 2;
    bg.spacingX = 40.0;
    bg.spacingY = 40.0;
    bg.params.holeDiameter = 22.0;
    f.doc.pushSnapshot();
    f.doc.addBoltGroup(bg, WireframeModel::cube(10.0));
    f.bolt = f.doc.boltGroups().back().id;
    f.boltDisplay = f.doc.boltGroups().back().displayEntityId;

    ComponentInstanceRecord rec;
    rec.typeId = L"end_plate";
    rec.mainPart = f.beam;
    rec.secondaryParts = {f.column};
    rec.ownedPlateIds = {f.plate};
    rec.ownedBoltGroupIds = {f.bolt};
    rec.origin = {100, 100, 0};
    rec.frame.frame.origin = {100, 100, 0};
    ComponentChildRecord plateChild;
    plateChild.roleKey = "plate";
    plateChild.kind = ComponentObjectKind::Plate;
    plateChild.entityId = f.plate;
    plateChild.plate = PlateSpec{};
    ComponentChildRecord boltChild;
    boltChild.roleKey = "bolts";
    boltChild.kind = ComponentObjectKind::BoltGroup;
    boltChild.entityId = f.bolt;
    boltChild.boltGroup = bg;
    rec.children = {plateChild, boltChild};
    f.doc.pushSnapshot();
    f.doc.addComponentInstance(std::move(rec));
    return f;
}

int main() {
    std::printf("== A: komponent kopyasi (tam secim) ==\n");
    {
        Document empty;
        Fixture f = makeJoint(empty);
        Document& doc = f.doc;
        check(doc.componentInstances().size() == 1, "A: baslangicta 1 component");
        const std::size_t modelsBefore = doc.models().size();
        const std::size_t boltsBefore = doc.boltGroups().size();
        const std::size_t holesBefore = doc.cutModifiers().size();
        const ComponentInstanceRecord before = doc.componentInstances().front();

        // TUM modelleri kopyala (kiris + kolon + levha + bulon montaji).
        std::vector<std::size_t> all;
        for (std::size_t i = 0; i < modelsBefore; ++i) all.push_back(i);
        doc.pushSnapshot();
        doc.copyModels(all, Vec3{2000, 0, 0});

        const EntityId beamCopy = doc.modelIds()[modelsBefore + 0];
        const EntityId columnCopy = doc.modelIds()[modelsBefore + 1];
        const EntityId plateCopy = doc.modelIds()[modelsBefore + 2];
        const EntityId boltDisplayCopy = doc.modelIds()[modelsBefore + 3];

        check(doc.componentInstances().size() == 2, "A: kopya COMPONENT olarak acildi (2 instance)");
        const ComponentInstanceRecord& clone = doc.componentInstances().back();
        check(clone.id != before.id && clone.id != kInvalidEntityId, "A: kopya YENI component kimligi aldi");
        check(clone.typeId == before.typeId, "A: component tipi korunur (end_plate)");
        check(clone.mainPart == beamCopy, "A: mainPart KOPYA kirisi gosterir");
        check(clone.secondaryParts.size() == 1 && clone.secondaryParts[0] == columnCopy,
              "A: secondaryPart KOPYA kolonu gosterir");
        check(clone.ownedPlateIds.size() == 1 && clone.ownedPlateIds[0] == plateCopy,
              "A: sahip olunan levha KOPYA levhaya eslendi");
        check(std::fabs(clone.origin.x - (before.origin.x + 2000.0)) < 1e-9 &&
                  std::fabs(clone.frame.frame.origin.x - (before.frame.frame.origin.x + 2000.0)) < 1e-9,
              "A: cerceve + origin otelemeyle tasindi");

        check(doc.boltGroups().size() == boltsBefore + 1, "A: bulon grubu da klonlandi");
        const BoltGroup& boltClone = doc.boltGroups().back();
        check(boltClone.id != f.bolt && clone.ownedBoltGroupIds.size() == 1 &&
                  clone.ownedBoltGroupIds[0] == boltClone.id,
              "A: kopya component KOPYA bulon grubunu sahiplenir");
        check(boltClone.displayEntityId == boltDisplayCopy,
              "A: bulon montaji kopyanin gorsel modelini kullanir (cift montaj yok)");
        check(std::fabs(boltClone.origin.x - (before.origin.x + 2000.0)) < 1e-9,
              "A: bulon grubu otelemeyle tasindi");
        bool partsRemapped = boltClone.parts.size() == 2;
        for (const EntityId p : boltClone.parts)
            if (p != beamCopy && p != plateCopy) partsRemapped = false;
        check(partsRemapped, "A: bulon grubu KOPYA parcalara baglandi");
        bool childRemapped = clone.children.size() == 2;
        for (const ComponentChildRecord& c : clone.children) {
            if (c.roleKey == "plate" && c.entityId != plateCopy) childRemapped = false;
            if (c.roleKey == "bolts" && c.entityId != boltClone.id) childRemapped = false;
        }
        check(childRemapped, "A: child tablosu (roleKey -> EntityId) kopyalara eslendi");
        check(doc.cutModifiers().size() > holesBefore, "A: kopya delikleri KOPYA parcalara delindi");

        // Tek undo adimi: kopya ile gelen her sey geri alinir, orijinal kalir.
        check(doc.undo(), "A: undo calisti");
        check(doc.componentInstances().size() == 1, "A: undo -> kopya component kalkti");
        check(doc.boltGroups().size() == boltsBefore, "A: undo -> kopya bulon grubu kalkti");
        check(doc.models().size() == modelsBefore, "A: undo -> kopya modeller kalkti");
        check(doc.componentInstances().front().id == before.id, "A: orijinal component korundu");
    }

    std::printf("== B: yarim kopya (ana parca secilmedi) -> instance ACILMAZ ==\n");
    {
        Document empty;
        Fixture f = makeJoint(empty);
        Document& doc = f.doc;
        const std::size_t modelsBefore = doc.models().size();
        const std::size_t instBefore = doc.componentInstances().size();
        const std::size_t boltsBefore = doc.boltGroups().size();
        // Yalniz levha modelini kopyala (kiris/kolon secilmedi).
        const std::size_t plateIndex = *doc.indexOfEntity(f.plate);
        doc.pushSnapshot();
        doc.copyModels({plateIndex}, Vec3{2000, 0, 0});
        check(doc.componentInstances().size() == instBefore,
              "B: ana parca kopyalanmadi -> yeni component ACILMADI (cift sahiplik yok)");
        check(doc.boltGroups().size() == boltsBefore, "B: bulon grubu da klonlanmadi");
        check(doc.models().size() == modelsBefore + 1, "B: levha yine kopyalandi (geometri kaybolmaz)");
        bool ownsCopy = false;
        for (const EntityId id : doc.componentInstances().front().ownedPlateIds)
            if (id == doc.modelIds().back()) ownsCopy = true;
        check(!ownsCopy, "B: kopya levha orijinal component'in sahipligi ALTINA girmadi");
    }

    if (failures) std::printf("%d FAIL\n", failures);
    else std::printf("ALL PASS\n");
    return failures == 0 ? 0 : 1;
}
