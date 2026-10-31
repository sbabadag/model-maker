// MOCK <> DÜĞÜM TUTARLILIĞI (portal_1.mmw geometrisi).
//
// Kullanıcı isteği (Selahattin 2026-10-31): "mock view ana çizimdeki düğüm
// noktasıyla aynı uyumlu profiller ve ölçüler kullanmalı".
//
// Mock (ComponentLivePreviewWidget) da, ana modeldeki düğüm de AYNI
// GenerationPlan'dan beslenir. Bu test bunu ÖLÇER: gerçek OCC ile
// materialize edilen alın levhasının bbox'ı, plandaki PlateSpec ölçüleriyle
// (genişlik/yükseklik/kalınlık/konum) ve kiriş kesim düzlemiyle birebir
// uyuşmalı. Uyuşmazsa mock ile ana çizim farklı ölçü gösterir.
#include "model_maker/component_engine.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_materializer.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/profile_database.hpp"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cmath>
#include <cstdio>

using namespace mm;
namespace {
int failures = 0;
void ck(bool x, const char* what) {
    std::printf("%s %s\n", x ? "PASS" : "FAIL", what);
    if (!x) ++failures;
}
bool near(double a, double b, double tol = 1.0) { return std::fabs(a - b) <= tol; }
SteelProfile profile(const char* n, double h, double b, double tw, double tf) {
    SteelProfile p; p.name = n; p.height = h; p.width = b;
    p.plateThickness = tw; p.flangeThickness = tf; return p;
}
struct Box3 { double x0{0}, y0{0}, z0{0}, x1{0}, y1{0}, z1{0}; };
Box3 bboxOf(const TopoDS_Shape& s) {
    Bnd_Box box; BRepBndLib::Add(s, box);
    Box3 o; box.Get(o.x0, o.y0, o.z0, o.x1, o.y1, o.z1); return o;
}
} // namespace

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    // portal_1.mmw: HEA240 kolon (90° dönük, eksen kesitin -x yüzünde) +
    // IPE400 eğik mahya kirişi (Top ankraj).
    const SteelProfile colProf = profile("HEA240", 230.0, 240.0, 7.5, 12.0);
    const SteelProfile beamProf = profile("IPE400", 400.0, 180.0, 8.6, 13.5);
    const Vec3 colFrom{-115.0, 12000.0, 0.0}, colTo{-115.0, 12000.0, 5000.0};
    const Vec3 beamFrom{0.0, 12000.0, 5000.0}, beamTo{5997.8, 12000.0, 5986.7};

    Document doc;
    doc.setOccStore(makeOccSolidStore());
    auto* store = asSolidStore(&doc.occStore());

    const TopoDS_Shape colShape = extrudeProfileSolid(colProf, colFrom, colTo, 90.0,
                                                      {0.0, 0.0, 1.0}, SectionAnchor::Top);
    const TopoDS_Shape beamShape = extrudeProfileSolid(beamProf, beamFrom, beamTo, 0.0,
                                                       {0.0, 0.0, 1.0}, SectionAnchor::Top);
    {
        WireframeModel m = shapeToWireframeWithFaces(colShape, 0.15);
        auto p = m.properties();
        p.profileName = "HEA240";
        p.axisFromX = colFrom.x; p.axisFromY = colFrom.y; p.axisFromZ = colFrom.z;
        p.axisToX = colTo.x; p.axisToY = colTo.y; p.axisToZ = colTo.z;
        p.sectionNormalZ = 1.0; p.profileRotation = 90.0; p.sectionAnchor = SectionAnchor::Top;
        m.setProperties(std::move(p));
        doc.addModel(std::move(m));
    }
    const EntityId colId = doc.modelId(0);
    store->set(colId, colShape);
    store->setBaseVersion(colId, store->activeVersion(colId));
    {
        WireframeModel m = shapeToWireframeWithFaces(beamShape, 0.15);
        auto p = m.properties();
        p.profileName = "IPE400";
        p.axisFromX = beamFrom.x; p.axisFromY = beamFrom.y; p.axisFromZ = beamFrom.z;
        p.axisToX = beamTo.x; p.axisToY = beamTo.y; p.axisToZ = beamTo.z;
        p.sectionNormalZ = 1.0; p.sectionAnchor = SectionAnchor::Top;
        m.setProperties(std::move(p));
        doc.addModel(std::move(m));
    }
    const EntityId beamId = doc.modelId(1);
    store->set(beamId, beamShape);
    store->setBaseVersion(beamId, store->activeVersion(beamId));

    ComponentInput in;
    in.main.id = colId;
    in.main.axisFrom = colFrom;
    in.main.axisTo = colTo;
    in.main.refNormal = {0.0, 0.0, 1.0};
    in.main.secWidth = 240.0; in.main.secHeight = 230.0;
    in.main.secWebThickness = 7.5; in.main.secFlangeThickness = 12.0;
    in.main.profileRotation = 90.0; in.main.sectionCenterOffset = -115.0;
    MemberGeometry g;
    g.id = beamId;
    g.axisFrom = beamFrom; g.axisTo = beamTo;
    g.refNormal = {0.0, 0.0, 1.0};
    g.secWidth = 180.0; g.secHeight = 400.0;
    g.secWebThickness = 8.6; g.secFlangeThickness = 13.5;
    g.sectionCenterOffset = -200.0;
    in.secondary = {g};

    const GenerationPlan plan = ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
    ck(plan.ok, "portal: plan uretildi");
    if (!plan.ok) { std::printf("  error=%ls\n", plan.error.c_str()); return 1; }

    // Mock'un gösterdiği alın levhası ölçüleri
    std::size_t ep = plan.plates.size();
    for (std::size_t i = 0; i < plan.plateKeys.size(); ++i)
        if (plan.plateKeys[i] == "haunch_end_plate") ep = i;
    ck(ep < plan.plates.size(), "portal: alin levhasi planda var");
    if (ep >= plan.plates.size()) return 1;
    const PlateSpec mockPlate = plan.plates[ep];

    // Ana modeldeki (OCC) aynı düğüm
    const ComponentPlan cp = componentPlanFromGenerationPlan(plan);
    ck(cp.ok, "portal: ComponentPlan gecerli");
    if (!cp.ok) return 1;
    ComponentMaterializer mat(doc);
    auto prep = mat.prepare(cp, colId, {beamId});
    ck(prep.ok, "portal: OCC hazirlik");
    if (!prep.ok) { std::printf("  prepare=%ls\n", prep.error.c_str()); return 1; }
    ComponentInstanceRecord rec;
    rec.typeId = L"tekla_haunch_40";
    rec.schemaVersion = 1;
    rec.mainPart = colId;
    rec.secondaryParts = {beamId};
    const auto* type = ComponentRegistry::instance().findType(rec.typeId);
    rec.params = alignParamsToSpecs(type->params, in.params);
    auto result = mat.commit(prep, rec);
    ck(result.ok, "portal: düğüm commit edildi");
    if (!result.ok) { std::printf("  commit=%ls\n", result.error.c_str()); return 1; }

    const auto plateIt = result.roleToEntity.find("haunch_end_plate");
    ck(plateIt != result.roleToEntity.end(), "portal: levha EntityId atandi");
    if (plateIt != result.roleToEntity.end()) {
        const TopoDS_Shape* shape = store->find(plateIt->second);
        ck(shape && !shape->IsNull(), "portal: levha gercek OCC katisi");
        if (shape && !shape->IsNull()) {
            const Box3 b = bboxOf(*shape);
            const double nodeX0 = b.x0, nodeX1 = b.x1, nodeZ0 = b.z0, nodeZ1 = b.z1;
            const double mockX0 = mockPlate.center.x - std::fabs(mockPlate.axis.x) * mockPlate.thickness * 0.5;
            const double mockX1 = mockPlate.center.x + std::fabs(mockPlate.axis.x) * mockPlate.thickness * 0.5;
            const double mockZ0 = mockPlate.center.z - mockPlate.height * 0.5;
            const double mockZ1 = mockPlate.center.z + mockPlate.height * 0.5;
            std::printf("       mock levha x[%.2f %.2f] z[%.2f %.2f]\n", mockX0, mockX1, mockZ0, mockZ1);
            std::printf("       node levha x[%.2f %.2f] z[%.2f %.2f]\n", nodeX0, nodeX1, nodeZ0, nodeZ1);
            ck(near(mockX0, nodeX0) && near(mockX1, nodeX1),
               "mock ve node ayni levha KALINLIGI/konumu (x)");
            ck(near(mockZ0, nodeZ0) && near(mockZ1, nodeZ1),
               "mock ve node ayni levha YUKSEKLIGI/konumu (z)");
        }
    }

    // Kiriş kesim düzlemi ile modeldeki kiriş ucu aynı olmalı.
    ck(plan.cuts.size() == 1, "portal: tek kiris fitting kesimi");
    if (!plan.cuts.empty()) {
        const double cutX = plan.cuts.front().planeOrigin.x;
        const TopoDS_Shape* beamShapeNow = store->find(beamId);
        ck(beamShapeNow && !beamShapeNow->IsNull(), "portal: kiris katisi var");
        if (beamShapeNow && !beamShapeNow->IsNull()) {
            const Box3 b = bboxOf(*beamShapeNow);
            std::printf("       kesim x=%.2f  kiris kati x0=%.2f\n", cutX, b.x0);
            ck(near(cutX, b.x0, 2.0), "kesim duzlemi ile kiris ucu ayni yerde");
        }
    }

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAIL\n", failures);
    return failures == 0 ? 0 : 1;
}
