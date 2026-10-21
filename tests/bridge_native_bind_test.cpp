// MODEL MAKER BRIDGE — native uye baglama testi (OCCT'siz ctest).
//
// Dogrular:
//   1) Document'ta iliskilendirilmis Tekla GUID bulunursa o uye kullanilir.
//   2) Profil uyumsuzsa conflict (otomatik secim YAPILMAZ).
//   3) GUID yok + secim yok -> needsSelection (eksik uye acikca raporlanir).
//   4) Kullanici secimi (BridgeMemberSelection) GUID yokken uyeyi baglar.
//   5) MMW8 round-trip: setTeklaGuid sonrasi save->load GUID eslemesi korunur.
//   6) Silinen varliga ait GUID lookup gecersiz (dangling koruma).

#include "model_maker/bridge_native_bind.hpp"
#include "model_maker/bridge_native_mapper.hpp"
#include "model_maker/bridge_package.hpp"
#include "model_maker/document.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static SteelProfile makeProfile(const char* name, double h, double b) {
    SteelProfile p;
    p.name = name;
    p.height = h;
    p.width = b;
    return p;
}

// Iki uyeli (kolon + kiris) bir Document kurar. beamProfile bos degilse kiris
// profili o olur (conflict testi icin IPE200 vb).
static Document makeDoc(EntityId& colId, EntityId& beamId,
                        const char* beamProfile = "HEA300") {
    Document doc;
    WireframeModel col = WireframeModel::cube(300.0);
    {
        EntityProperties p = col.properties();
        p.profileName = "HEA400";
        p.axisFromZ = 0; p.axisToZ = 3000;
        col.setProperties(std::move(p));
    }
    doc.addModel(std::move(col));
    colId = doc.modelId(0);

    WireframeModel beam = WireframeModel::cube(300.0);
    {
        EntityProperties p = beam.properties();
        p.profileName = beamProfile;
        p.axisFromY = 0; p.axisToY = 4000;
        beam.setProperties(std::move(p));
    }
    doc.addModel(std::move(beam));
    beamId = doc.modelId(1);
    return doc;
}

// mapBridgeToNative icin tutarli cok kolonlu paket (mapper testindekiyle ayni).
static BridgePackage makePackage() {
    BridgePackage pkg;
    pkg.manifest.sourceComponentNumber = 146;
    pkg.manifest.sourceComponentName = "Shear plate simple";
    pkg.component.number = 146;
    pkg.component.name = "Shear plate simple";
    pkg.component.primaryMember.guid = "c1";
    pkg.component.primaryMember.profile = "HEA400";
    pkg.component.primaryMember.role = "COLUMN";
    pkg.component.primaryMember.startPoint = BridgeVec3{0, 0, 0};
    pkg.component.primaryMember.endPoint = BridgeVec3{0, 0, 3000};
    BridgeMemberRef beam;
    beam.guid = "b1";
    beam.profile = "HEA300";
    beam.role = "BEAM";
    beam.startPoint = BridgeVec3{0, 0, 3000};
    beam.endPoint = BridgeVec3{0, 4000, 3000};
    pkg.component.secondaryMembers.push_back(beam);

    BridgeObject plate;
    plate.kind = "ContourPlate";
    plate.sourceGUID = "p1";
    plate.profile = "PL10";
    plate.material = "S235JR";
    plate.bbox = BridgeBBox{{0, 0, 0}, {10, 170, 200}};
    pkg.objects.push_back(plate);

    BridgeObject bg;
    bg.kind = "BoltGroup";
    bg.sourceGUID = "bg1";
    bg.boltGroup = BridgeBoltGroupInfo{};
    bg.boltGroup->size = "M20";
    bg.boltGroup->axis = "x";
    bg.boltGroup->direction = BridgeVec3{1, 0, 0};
    for (double z : {40.0, 100.0, 160.0})
        for (double y : {55.0, 115.0}) {
            BridgeBolt b;
            b.position = BridgeVec3{5, y, z};
            b.nominalDiameter = 20;
            b.holeDiameterSource = "standard";
            bg.boltGroup->bolts.push_back(b);
        }
    pkg.objects.push_back(bg);
    return pkg;
}

int main() {
    std::vector<SteelProfile> catalog = {makeProfile("HEA400", 300, 300),
                                         makeProfile("HEA300", 300, 200)};

    // --- 1) GUID map ile baglama: hem primary hem secondary GUID'den cozulur ---
    {
        EntityId colId, beamId;
        Document doc = makeDoc(colId, beamId);
        doc.setTeklaGuid(colId, "c1");
        doc.setTeklaGuid(beamId, "b1");

        const BridgeNativeMapping m = mapBridgeToNative(makePackage(), catalog);
        const BridgeMemberBinding b = resolveBridgeMemberBinding(doc, m, {});
        check(b.ok, "GUID map: baglama ok");
        check(!b.needsSelection, "GUID map: secim gerekmez");
        check(b.primary.resolved && b.primary.entityId == colId, "primary -> kolon EntityId");
        check(b.secondary.size() == 1 && b.secondary[0].resolved &&
                  b.secondary[0].entityId == beamId,
              "secondary -> kiris EntityId");
    }

    // --- 2) Profil uyumsuz -> conflict (otomatik secim YAPILMAZ) --------------
    {
        EntityId colId, beamId;
        Document doc = makeDoc(colId, beamId, "IPE200");  // kiris profili yanlis
        doc.setTeklaGuid(colId, "c1");
        doc.setTeklaGuid(beamId, "b1");

        const BridgeNativeMapping m = mapBridgeToNative(makePackage(), catalog);
        const BridgeMemberBinding b = resolveBridgeMemberBinding(doc, m, {});
        check(!b.ok, "profil uyumsuz: ok=false");
        check(b.needsSelection, "profil uyumsuz: needsSelection");
        check(b.secondary[0].conflict, "profil uyumsuz: conflict isaretli");
    }

    // --- 3) GUID yok + secim yok -> needsSelection ----------------------------
    {
        EntityId colId, beamId;
        Document doc = makeDoc(colId, beamId);  // hic GUID yok
        const BridgeNativeMapping m = mapBridgeToNative(makePackage(), catalog);
        const BridgeMemberBinding b = resolveBridgeMemberBinding(doc, m, {});
        check(!b.ok, "GUID yok: ok=false");
        check(b.needsSelection, "GUID yok: needsSelection");
        check(!b.missing.empty(), "GUID yok: missing dolu (diagnostic)");
    }

    // --- 4) Kullanici secimi GUID yokken uyeyi baglar -------------------------
    {
        EntityId colId, beamId;
        Document doc = makeDoc(colId, beamId);  // hic GUID yok
        const BridgeNativeMapping m = mapBridgeToNative(makePackage(), catalog);
        BridgeMemberSelection sel;
        sel.primaryId = colId;
        sel.secondaryIds = {beamId};
        const BridgeMemberBinding b = resolveBridgeMemberBinding(doc, m, sel);
        check(b.ok, "kullanici secimi: baglama ok");
        check(b.primary.resolved && b.primary.entityId == colId, "secim: primary");
        check(b.secondary[0].resolved && b.secondary[0].entityId == beamId, "secim: secondary");
    }

    // --- 5) MMW8 round-trip: GUID eslemesi korunur ----------------------------
    {
        EntityId colId, beamId;
        Document doc = makeDoc(colId, beamId);
        doc.setTeklaGuid(colId, "c1");
        doc.setTeklaGuid(beamId, "b1");

        const std::string path = "/tmp/bridge_bind_roundtrip.mmw8";
        doc.save(path);

        Document doc2;
        doc2.load(path);

        check(doc2.entityForTeklaGuid("c1").has_value() &&
                  *doc2.entityForTeklaGuid("c1") == colId,
              "round-trip: c1 -> kolon korundu");
        check(doc2.entityForTeklaGuid("b1").has_value() &&
                  *doc2.entityForTeklaGuid("b1") == beamId,
              "round-trip: b1 -> kiris korundu");
    }

    // --- 6) Silinen varliga ait GUID gecersiz (dangling koruma) ---------------
    {
        EntityId colId, beamId;
        Document doc = makeDoc(colId, beamId);
        doc.setTeklaGuid(beamId, "b1");
        const auto idx = doc.indexOfEntity(beamId);
        if (idx.has_value()) doc.deleteModels({*idx});
        check(!doc.entityForTeklaGuid("b1").has_value(),
              "silinen varligin GUID'i gecersiz");
    }

    std::printf("FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
