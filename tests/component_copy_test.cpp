// KOPYA: component'ler COMPONENT olarak kopyalanir (C3).
// Kullanici geri bildirimi (2026-10-28): "elemanlar kopyalaniyor ama component
// olarak kopyalanmiyor" — kopya, sahipsiz model yigini olarak kaliyordu.
// Kural: kopyalanan modellerin sahip oldugu component instance'i + bulon grubu
// KLONLANIR; kimlikler kopyalara eslenir, cerceve/yerlesim otelemeyle tasinir,
// tek undo adimi; YARIM kopyada (ana parca kopyalanmadiysa) instance ACILMAZ.
#include "model_maker/document.hpp"

#include <algorithm>
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

    std::printf("== C: component KENDI nesneleriyle kopyalanir (ana parca secilmedi) ==\n");
    {
        Document empty;
        Fixture f = makeJoint(empty);
        Document& doc = f.doc;
        const std::size_t instBefore = doc.componentInstances().size();
        const EntityId beam = f.beam;
        // Levha + bulon montaji (component'in SAHIP oldugu nesneler) kopyalanir.
        const std::size_t plateIndex = *doc.indexOfEntity(f.plate);
        const std::size_t boltIndex = *doc.indexOfEntity(f.boltDisplay);
        doc.pushSnapshot();
        doc.copyModels({plateIndex, boltIndex}, Vec3{3000, 0, 0});
        check(doc.componentInstances().size() == instBefore + 1,
              "C: component kopyalandi (ana parca secilmese de instance acilir)");
        const ComponentInstanceRecord& clone = doc.componentInstances().back();
        check(clone.ownedBoltGroupIds.size() == 1 &&
                  clone.ownedBoltGroupIds[0] == doc.boltGroups().back().id,
              "C: kopya KOPYA bulon grubunu sahiplenir");
        check(clone.mainPart == beam, "C: kopyalanmayan ana parca referansi orijinalde kalir");
        check(std::fabs(clone.frame.frame.origin.x - 3100.0) < 1e-9,
              "C: kopya cercevesi yeni konuma tasindi");
    }

    std::printf("== D: kopyalanan component ASSEMBLY olusturur (weld klonu) ==\n");
    {
        // BUG (2026-10-28): assembly'ler SHOP weld grafindan turetilir; kopyada
        // weld kayitlari klonlanmadigi icin kopya parcalar assembly'ye girmiyordu.
        Document empty;
        Fixture f = makeJoint(empty);
        Document& doc = f.doc;
        doc.pushSnapshot();
        Weld weld;
        weld.mainPart = f.beam;
        weld.secondaryPart = f.column;
        weld.type = WeldType::Fillet;
        weld.size = 6.0;
        weld.weldClass = WeldClass::Shop;
        weld.origin = {50.0, 100.0, 0.0};
        const std::size_t wi = doc.addWeld(weld);
        const EntityId weldId = doc.welds()[wi].id;
        // Component child tablosuna kaynak kaydi (recompute bu kimligi kullanir).
        ComponentInstanceRecord rec = doc.componentInstances().front();
        ComponentChildRecord weldChild;
        weldChild.roleKey = "weld";
        weldChild.kind = ComponentObjectKind::Weld;
        weldChild.entityId = weldId;
        WeldSpec spec;
        spec.partA = f.beam;
        spec.partB = f.column;
        spec.size = 6.0;
        weldChild.weld = spec;
        rec.children.push_back(weldChild);
        doc.pushSnapshot();
        check(doc.setComponentInstance(0, rec), "D: component kaydina weld child eklendi");

        const std::size_t modelsBefore = doc.models().size();
        const std::size_t weldsBefore = doc.welds().size();
        const std::size_t asmBefore = doc.assemblies().size();
        check(weldsBefore == 1 && asmBefore == 1, "D: baslangic 1 kaynak + 1 assembly");

        std::vector<std::size_t> all;
        for (std::size_t i = 0; i < modelsBefore; ++i) all.push_back(i);
        doc.pushSnapshot();
        doc.copyModels(all, Vec3{2000, 0, 0});

        const EntityId beamCopy = doc.modelIds()[modelsBefore + 0];
        const EntityId columnCopy = doc.modelIds()[modelsBefore + 1];

        check(doc.welds().size() == weldsBefore + 1, "D: kaynak kaydi KOPYALANDI");
        const Weld& original = doc.welds().front();
        check(original.id == weldId && original.mainPart == f.beam && original.secondaryPart == f.column,
              "D: ORIJINAL kaynak yerinde (kopya onu isgal etmez)");
        const Weld& cloneWeld = doc.welds().back();
        check(cloneWeld.id != weldId, "D: kopya kaynak YENI kimlik aldi");
        check(cloneWeld.mainPart == beamCopy && cloneWeld.secondaryPart == columnCopy,
              "D: kopya kaynak KOPYA parcalara baglandi");
        check(std::fabs(cloneWeld.origin.x - (weld.origin.x + 2000.0)) < 1e-9,
              "D: kaynak konumu otelemeyle tasindi");

        check(doc.assemblies().size() == asmBefore + 1, "D: KOPYA da assembly olusturdu");
        const auto copyAsm = doc.assemblyIdContaining(beamCopy);
        check(copyAsm.has_value(), "D: kopya kiris bir assembly'de");
        if (copyAsm) {
            const auto* asmRec = [&]() -> const AssemblyRecord* {
                const auto idx = doc.indexOfAssembly(*copyAsm);
                return idx ? &doc.assemblies()[*idx] : nullptr;
            }();
            bool hasBoth = false;
            if (asmRec) {
                const bool hasBeam = std::find(asmRec->parts.begin(), asmRec->parts.end(), beamCopy) != asmRec->parts.end();
                const bool hasColumn = std::find(asmRec->parts.begin(), asmRec->parts.end(), columnCopy) != asmRec->parts.end();
                hasBoth = hasBeam && hasColumn;
            }
            check(hasBoth, "D: kopya assembly kopya kiris + kolonu icerir");
        }
        const auto originalAsm = doc.assemblyIdContaining(f.beam);
        check(originalAsm.has_value() && originalAsm != copyAsm,
              "D: orijinal assembly kopyadan ayri");
        // Child tablosu: kopya component'in weld child'i KOPYA kaynagi gosterir.
        bool childWeldRemapped = false;
        for (const ComponentChildRecord& c : doc.componentInstances().back().children)
            if (c.roleKey == "weld") childWeldRemapped = c.entityId == cloneWeld.id;
        check(childWeldRemapped, "D: child tablosundaki weld KOPYA kaynaga eslendi");

        check(doc.undo(), "D: undo calisti");
        check(doc.welds().size() == weldsBefore, "D: undo -> kopya kaynak kalkti");
        check(doc.assemblies().size() == asmBefore, "D: undo -> kopya assembly kalkti");
        check(doc.welds().front().id == weldId, "D: undo -> orijinal kaynak korundu");
    }

    std::printf("== E: yarim kopyada kaynak klonlanmaz (dangling weld yok) ==\n");
    {
        Document empty;
        Fixture f = makeJoint(empty);
        Document& doc = f.doc;
        doc.pushSnapshot();
        Weld weld;
        weld.mainPart = f.beam;
        weld.secondaryPart = f.column;
        weld.weldClass = WeldClass::Shop;
        doc.addWeld(weld);
        const std::size_t weldsBefore = doc.welds().size();
        // Yalniz levha kopyalanir: kaynagin iki parcasi da kopya degil.
        const std::size_t plateIndex = *doc.indexOfEntity(f.plate);
        doc.pushSnapshot();
        doc.copyModels({plateIndex}, Vec3{2000, 0, 0});
        check(doc.welds().size() == weldsBefore, "E: yarim kopyada weld KLONLANMAZ");
    }

    std::printf("== F: LEVHA + KAYNAK kopyalanir -> yeni konumdaki uyeye baglanir ==\n");
    {
        // KULLANICI RAPORU (2026-10-28): "kaynakla beraber levhayi kopyaladigimda
        // assembly olusturmadi". Kural: tek ucu kopya ise kaynak KLONLANIR;
        // kopyalanan uc kopyaya, diger uc ayni konumdaki uyeye baglanir (orijinal
        // partner hala temasta ise o, degilse en yakin yapisal uye).
        Document doc;
        auto addMember = [&doc](double x0, double y0, double x1, double y1) {
            doc.pushSnapshot();
            WireframeModel m = WireframeModel::rectangle({x0, y0, 0.0}, {x1, y1, 0.0});
            EntityProperties props = m.properties();
            props.profileName = "IPE200";
            props.axisFromX = x0; props.axisFromY = y0; props.axisFromZ = 0.0;
            props.axisToX = x1; props.axisToY = y1; props.axisToZ = 0.0;
            m.setProperties(std::move(props));
            doc.addModel(std::move(m));
            return doc.modelIds().back();
        };
        const EntityId beam1 = addMember(0, 0, 100, 100);
        const EntityId beam2 = addMember(1500, 0, 1600, 100);
        doc.pushSnapshot();
        doc.addModel(rect(50, 100, 0));      // levha: 50..150, 100..200
        const EntityId plate = doc.modelIds().back();

        doc.pushSnapshot();
        Weld weld;
        weld.mainPart = beam1;
        weld.secondaryPart = plate;
        weld.weldClass = WeldClass::Shop;
        weld.origin = {60.0, 100.0, 0.0};
        const std::size_t wi =
            doc.addWeld(weld, WireframeModel::rectangle({50, 100, 0}, {70, 120, 0}));
        const EntityId weldId = doc.welds()[wi].id;
        const EntityId weldDisplay = doc.welds()[wi].displayEntityId;
        check(weldDisplay != kInvalidEntityId, "F: kaynak gorsel modeli var");
        check(doc.assemblies().size() == 1, "F: baslangic 1 assembly (beam1 + levha)");

        const std::size_t weldsBefore = doc.welds().size();
        // Secim: LEVHA + KAYNAK -> beam2'nin yanina (1500 mm).
        const std::size_t plateIdx = *doc.indexOfEntity(plate);
        const std::size_t weldIdx = *doc.indexOfEntity(weldDisplay);
        doc.pushSnapshot();
        doc.copyModels({plateIdx, weldIdx}, Vec3{1500, 0, 0});
        const EntityId plateCopy = doc.modelIds()[doc.models().size() - 2];
        const EntityId weldDisplayCopy = doc.modelIds()[doc.models().size() - 1];

        check(doc.welds().size() == weldsBefore + 1,
              "F: kaynak KLONLANDI (tek uclu kopyada da)");
        if (doc.welds().size() == weldsBefore + 1) {
            const Weld& clone = doc.welds().back();
            check(clone.id != weldId, "F: kopya kaynak yeni kimlik");
            check(clone.secondaryPart == plateCopy, "F: kopya kaynak KOPYA levhaya bagli");
            check(clone.mainPart == beam2,
                  "F: kopya kaynak YENI KONUMDAKI uyeye baglandi (beam2)");
            check(clone.mainPart != beam1, "F: kopya kaynak uzaktaki orijinal uyeye baglanmadi");
            check(clone.displayEntityId == weldDisplayCopy,
                  "F: kopya kaynak secilen gorsel modeli kullanir");
            check(std::fabs(clone.origin.x - (weld.origin.x + 1500.0)) < 1e-9,
                  "F: kaynak konumu otelemeyle tasindi");
        }
        const Weld& originalWeld = doc.welds().front();
        check(originalWeld.id == weldId && originalWeld.secondaryPart == plate &&
                  originalWeld.mainPart == beam1,
              "F: ORIJINAL kaynak yerinde");
        const auto asmBeam2 = doc.assemblyIdContaining(beam2);
        check(asmBeam2.has_value() && doc.assemblyIdContaining(plateCopy) == asmBeam2,
              "F: KOPYA LEVHA artik bir ASSEMBLY uyesi (beam2 ile)");
        check(doc.assemblyIdContaining(beam1) != asmBeam2,
              "F: orijinal assembly kopyadan ayri");

        check(doc.undo(), "F: undo calisti");
        check(doc.welds().size() == weldsBefore, "F: undo -> kopya kaynak kalkti");
        check(doc.assemblyIdContaining(plate) != std::nullopt, "F: undo -> orijinal levha assembly'de");
    }

    std::printf("== G: temas eden uye yoksa kaynak klonlanmaz (uydurma kayit yok) ==\n");
    {
        Document doc;
        doc.pushSnapshot();
        WireframeModel m = WireframeModel::rectangle({0, 0, 0}, {100, 100, 0});
        EntityProperties props = m.properties();
        props.profileName = "IPE200";
        m.setProperties(std::move(props));
        doc.addModel(std::move(m));
        const EntityId beam = doc.modelIds().back();
        doc.pushSnapshot();
        doc.addModel(rect(50, 100, 0));
        const EntityId plate = doc.modelIds().back();
        doc.pushSnapshot();
        Weld weld;
        weld.mainPart = beam;
        weld.secondaryPart = plate;
        weld.weldClass = WeldClass::Shop;
        doc.addWeld(weld);
        const std::size_t weldsBefore = doc.welds().size();
        // Bos alana kopyala: 3000 mm otede hicbir uye yok.
        doc.pushSnapshot();
        doc.copyModels({*doc.indexOfEntity(plate)}, Vec3{3000, 0, 0});
        check(doc.welds().size() == weldsBefore,
              "G: temas eden uye yok -> kaynak KLONLANMAZ (metrelerce otede kayit yok)");
    }

    if (failures) std::printf("%d FAIL\n", failures);
    else std::printf("ALL PASS\n");
    return failures == 0 ? 0 : 1;
}
