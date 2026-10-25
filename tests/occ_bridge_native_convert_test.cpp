// MODEL MAKER BRIDGE — native parametrik donusum (convertBridgeToNative) OCC testi.
//
// Dogrular (gercek OCCT):
//   1) GUID map'li 2 uyeli belgede convert ok -> tekla_shear_plate_146 component,
//      2x3 bulon (12 delik), delikli levha hacmi analitik.
//   2) Uyeler bagli degil (GUID yok + secim yok) -> needsMemberSelection, DONUSUM YOK,
//      orijinal belge DEGISMEDI (component yok).
//   3) Basarili convert sonrasi Tekla GUID iliskileri Document'a yazilir.

#include "model_maker/bridge_occ_adapter.hpp"
#include "model_maker/bridge_native_bind.hpp"
#include "model_maker/bridge_package.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-3) { return std::fabs(a - b) < tol; }

static SteelProfile mk(const char* n, double h, double b, double tw, double tf) {
    SteelProfile p; p.name = n; p.height = h; p.width = b; p.plateThickness = tw; p.flangeThickness = tf; return p;
}

// Kolon (HEB300 dikey) + kiris (IPE200 yatay) + OCC store kurar.
struct S {
    Document doc;
    OccSolidStore* store{nullptr};
    EntityId colId{kInvalidEntityId}, beamId{kInvalidEntityId};
};
static S makeSetup() {
    S s;
    const SteelProfile heb300 = mk("HEB300", 300, 300, 11, 19);
    const SteelProfile ipe200 = mk("IPE200", 200, 100, 5.6, 8.5);
    const TopoDS_Shape colSolid = extrudeProfileSolid(heb300, {0,0,0}, {0,0,3000}, 0.0, {0,1,0});
    const TopoDS_Shape beamSolid = extrudeProfileSolid(ipe200, {0,1000,1500}, {0,60,1500}, 0.0, {0,0,1});
    s.doc.setOccStore(makeOccSolidStore());
    s.store = asSolidStore(&s.doc.occStore());

    WireframeModel cm = shapeToWireframeWithFaces(colSolid, 0.15);
    { EntityProperties p = cm.properties(); p.profileName = "HEB300"; p.axisFromZ = 0; p.axisToZ = 3000; p.sectionNormalY = 1; cm.setProperties(std::move(p)); }
    s.doc.addModel(std::move(cm));
    s.colId = s.doc.modelId(0);
    s.store->set(s.colId, colSolid);
    s.store->setBaseVersion(s.colId, s.store->activeVersion(s.colId));

    WireframeModel bm = shapeToWireframeWithFaces(beamSolid, 0.15);
    { EntityProperties p = bm.properties(); p.profileName = "IPE200"; p.axisFromY = 1000; p.axisFromZ = 1500; p.axisToY = 60; p.axisToZ = 1500; p.sectionNormalZ = 1; bm.setProperties(std::move(p)); }
    s.doc.addModel(std::move(bm));
    s.beamId = s.doc.modelId(1);
    s.store->set(s.beamId, beamSolid);
    s.store->setBaseVersion(s.beamId, s.store->activeVersion(s.beamId));
    return s;
}

// 146 cok kolonlu (2x3) paket: 170x200x10, M20, 6 bulon. Uyeler HEB300/IPE200.
static BridgePackage makePackage() {
    BridgePackage pkg;
    pkg.manifest.sourceComponentNumber = 146;
    pkg.manifest.sourceComponentName = "Shear plate simple";
    pkg.component.number = 146;
    pkg.component.name = "Shear plate simple";
    pkg.component.primaryMember.guid = "c1";
    pkg.component.primaryMember.profile = "HEB300";
    pkg.component.primaryMember.role = "COLUMN";
    pkg.component.primaryMember.startPoint = BridgeVec3{0, 0, 0};
    pkg.component.primaryMember.endPoint = BridgeVec3{0, 0, 3000};
    BridgeMemberRef beam;
    beam.guid = "b1";
    beam.profile = "IPE200";
    beam.role = "BEAM";
    beam.startPoint = BridgeVec3{0, 1000, 1500};
    beam.endPoint = BridgeVec3{0, 60, 1500};
    pkg.component.secondaryMembers.push_back(beam);

    BridgeObject plate;
    plate.kind = "ContourPlate";
    plate.sourceGUID = "p1";
    plate.profile = "PL10";
    plate.material = "S235JR";
    plate.bbox = BridgeBBox{{4.25, 195, 6959}, {14.25, 365, 7159}};  // 10 x 170 x 200
    pkg.objects.push_back(plate);

    BridgeObject bg;
    bg.kind = "BoltGroup";
    bg.sourceGUID = "bg1";
    bg.boltGroup = BridgeBoltGroupInfo{};
    bg.boltGroup->size = "M20";
    bg.boltGroup->axis = "x";
    bg.boltGroup->axisSource = "measured";
    bg.boltGroup->partToBoltTo = "p1";
    bg.boltGroup->direction = BridgeVec3{1, 0, 0};
    for (double z : {6999.0, 7059.0, 7119.0})
        for (double y : {265.0, 325.0}) {
            BridgeBolt b;
            b.position = BridgeVec3{9.25, y, z};
            b.nominalDiameter = 20;
            b.holeDiameter = 22.0;
            b.holeDiameterSource = "measured";
            bg.boltGroup->bolts.push_back(b);
        }
    pkg.objects.push_back(bg);
    return pkg;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const std::vector<SteelProfile> catalog = {mk("HEB300", 300, 300, 11, 19),
                                               mk("IPE200", 200, 100, 5.6, 8.5)};

    // --- 1) GUID map ile convert ok -------------------------------------------
    {
        S s = makeSetup();
        s.doc.setTeklaGuid(s.colId, "c1");
        s.doc.setTeklaGuid(s.beamId, "b1");

        const BridgeNativeConvertOutcome r =
            convertBridgeToNative(s.doc, makePackage(), catalog, {});
        check(r.ok, "convert: ok");
        check(r.componentId != kInvalidEntityId, "convert: componentId");
        check(!r.needsMemberSelection, "convert: secim gerekmedi");

        const auto ci = s.doc.indexOfComponentInstance(r.componentId);
        check(ci.has_value(), "convert: instance kayitli");
        if (ci.has_value()) {
            check(s.doc.componentInstances()[*ci].typeId == L"tekla_shear_plate_146",
                  "convert: typeId tekla_shear_plate_146");
            check(s.doc.componentInstances()[*ci].mainPart == s.colId, "convert: mainPart kolon");
            check(s.doc.componentInstances()[*ci].secondaryParts.size() == 1 &&
                      s.doc.componentInstances()[*ci].secondaryParts[0] == s.beamId,
                  "convert: secondary kiris");
        }

        // Levha hacmi (6 delik, 22mm) analitik.
        const auto& rec = s.doc.componentInstances()[*ci];
        check(!rec.ownedPlateIds.empty(), "convert: levha var");

        // Parametreler kalici kayda yazildi (Properties paneli + recompute icin).
        {
            auto param = [&](const wchar_t* name) -> const CompParamValue* {
                for (const auto& p : rec.params)
                    if (p.name == name) return &p.value;
                return nullptr;
            };
            auto dbl = [](const CompParamValue* v) {
                return v && std::holds_alternative<double>(*v) ? std::get<double>(*v) : -1e9;
            };
            auto i64 = [](const CompParamValue* v) {
                return v && std::holds_alternative<int>(*v) ? std::get<int>(*v) : -1;
            };
            check(approx(dbl(param(L"plateThickness")), 10.0), "convert: param plateThickness=10");
            check(approx(dbl(param(L"plateWidth")), 170.0), "convert: param plateWidth=170");
            check(i64(param(L"boltCountX")) == 2, "convert: param boltCountX=2");
            check(i64(param(L"boltCount")) == 3, "convert: param boltCount(Y)=3");
        }

        if (!rec.ownedPlateIds.empty()) {
            const TopoDS_Shape* ts = s.store->find(rec.ownedPlateIds[0]);
            if (ts && !ts->IsNull()) {
                const double PI = std::acos(-1.0);
                const double vol = shapeVolume(*ts);
                const double expected = 340000.0 - 6.0 * PI * 11.0 * 11.0 * 10.0;
                check(approx(vol, expected, expected * 0.01), "convert: delikli levha hacmi 317192");
            }
        }

        // GUID iliskileri yazildi (zaten vardi; yine de dogrula).
        check(s.doc.entityForTeklaGuid("c1").has_value(), "convert: c1 GUID iliskisi");
        check(s.doc.entityForTeklaGuid("b1").has_value(), "convert: b1 GUID iliskisi");
    }

    // --- 1B) Unverified live-like bolt geometry must NOT generate native solid.
    {
        S s = makeSetup();
        s.doc.setTeklaGuid(s.colId, "c1");
        s.doc.setTeklaGuid(s.beamId, "b1");
        BridgePackage pkg = makePackage();
        // Same problematic combination observed on a live 2025 SP6 146 export.
        BridgeBoltGroupInfo& bg = *pkg.objects[1].boltGroup;
        bg.axis.clear();
        bg.axisSource.clear();
        for (auto& bolt : bg.bolts) {
            bolt.position.x = 0.0; // source X=0, plate X=4.25..14.25
            bolt.holeDiameter = 0.0;
            bolt.holeDiameterSource.clear();
        }
        const std::size_t oldComps = s.doc.componentInstances().size();
        const std::size_t oldModels = s.doc.models().size();
        const BridgeNativeConvertOutcome blocked =
            convertBridgeToNative(s.doc, pkg, catalog, {});
        check(!blocked.ok, "native safety: unverified source holes block conversion");
        check(!blocked.error.empty(), "native safety: diagnostic provided");
        check(s.doc.componentInstances().size() == oldComps &&
              s.doc.models().size() == oldModels,
              "native safety: Document unchanged after rejected conversion");
    }

    // --- 2) Uye bagli degil -> needsMemberSelection, donusum YOK ---------------
    {
        S s = makeSetup();  // hic GUID yok
        const std::size_t compsBefore = s.doc.componentInstances().size();
        const std::size_t modelsBefore = s.doc.models().size();

        const BridgeNativeConvertOutcome r =
            convertBridgeToNative(s.doc, makePackage(), catalog, {});
        check(!r.ok, "eksik uye: ok=false");
        check(r.needsMemberSelection, "eksik uye: needsMemberSelection");
        check(!r.missing.empty(), "eksik uye: missing dolu");
        check(s.doc.componentInstances().size() == compsBefore &&
                  s.doc.models().size() == modelsBefore,
              "eksik uye: belge DEGISMEDI");
    }

    // --- 3) Kullanici secimi ile convert (GUID yokken) -------------------------
    {
        S s = makeSetup();
        BridgeMemberSelection sel;
        sel.primaryId = s.colId;
        sel.secondaryIds = {s.beamId};
        const BridgeNativeConvertOutcome r =
            convertBridgeToNative(s.doc, makePackage(), catalog, sel);
        check(r.ok, "secim: convert ok");
        check(s.doc.entityForTeklaGuid("c1").has_value() &&
                  *s.doc.entityForTeklaGuid("c1") == s.colId,
              "secim: GUID iliskisi sonradan kuruldu");
    }

    std::printf("FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
