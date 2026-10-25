// Haunch (40) native OCC acceptance: registry -> real BRep -> Document
// -> recompute. No Tekla profile-name restriction or guessed source geometry.
#include "model_maker/component_engine.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_materializer.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/profile_database.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;
namespace {
int failures=0;
void ck(bool x,const char* what) {
    std::printf("%s %s\n",x?"PASS":"FAIL",what);
    if(!x)++failures;
}
SteelProfile profile(const char* n,double h,double b,double tw,double tf) {
    SteelProfile p;p.name=n;p.height=h;p.width=b;
    p.plateThickness=tw;p.flangeThickness=tf;return p;
}
struct Setup {
    Document doc;
    OccSolidStore* store=nullptr;
    EntityId col=0,beam=0;
    ComponentInput input;
};
Setup makeSetup() {
    Setup s;
    SteelProfile c=profile("HEB300",300,300,11,19);
    SteelProfile b=profile("IPE200",200,100,5.6,8.5);
    const TopoDS_Shape cs=extrudeProfileSolid(
        c,{0,0,0},{0,0,3000},0.0,{0,1,0},SectionAnchor::Center);
    const TopoDS_Shape bs=extrudeProfileSolid(
        b,{0,1200,1500},{0,60,1500},0.0,{0,0,1},SectionAnchor::Center);
    s.doc.setOccStore(makeOccSolidStore());
    s.store=asSolidStore(&s.doc.occStore());
    WireframeModel colM=shapeToWireframeWithFaces(cs,0.15);
    {
        auto prop=colM.properties();prop.profileName="HEB300";
        prop.axisFromZ=0;prop.axisToZ=3000;prop.sectionNormalY=1;
        colM.setProperties(std::move(prop));
    }
    s.doc.addModel(std::move(colM));s.col=s.doc.modelId(0);
    s.store->set(s.col,cs);
    s.store->setBaseVersion(s.col,s.store->activeVersion(s.col));
    WireframeModel beamM=shapeToWireframeWithFaces(bs,0.15);
    {
        auto prop=beamM.properties();prop.profileName="IPE200";
        prop.axisFromY=1200;prop.axisFromZ=1500;
        prop.axisToY=60;prop.axisToZ=1500;prop.sectionNormalZ=1;
        beamM.setProperties(std::move(prop));
    }
    s.doc.addModel(std::move(beamM));s.beam=s.doc.modelId(1);
    s.store->set(s.beam,bs);
    s.store->setBaseVersion(s.beam,s.store->activeVersion(s.beam));
    s.input.main.id=s.col;
    s.input.main.axisFrom={0,0,0};
    s.input.main.axisTo={0,0,3000};
    s.input.main.refNormal={0,1,0};
    s.input.main.secHeight=300;s.input.main.secWidth=300;
    s.input.main.secWebThickness=11;s.input.main.secFlangeThickness=19;
    MemberGeometry g;
    g.id=s.beam;
    g.axisFrom={0,1200,1500};g.axisTo={0,60,1500};
    g.refNormal={0,0,1};
    g.secHeight=200;g.secWidth=100;
    g.secWebThickness=5.6;g.secFlangeThickness=8.5;
    s.input.secondary={g};
    return s;
}
} // namespace

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    Setup s=makeSetup();
    GenerationPlan gp=ComponentRegistry::instance().generate(
        L"tekla_haunch_40",s.input);
    ck(gp.ok,"Haunch 40 generated");
    if(!gp.ok) {
        std::printf("Error: %ls\n",gp.error.c_str());
        return 1;
    }
    ck(gp.plates.size()==4,"4 generated plates");
    ck(gp.boltGroups.size()==2,"2x2 + 2x2 bolts");
    const ComponentPlan cp=componentPlanFromGenerationPlan(gp);
    ck(cp.ok,"ComponentPlan valid");
    if(!cp.ok) return 1;
    ComponentMaterializer mat(s.doc);
    auto prep=mat.prepare(cp,s.col,{s.beam});
    ck(prep.ok,"OCC BRep preparation");
    if(!prep.ok) {
        std::printf("Prepare failed: %ls\n",prep.error.c_str());
        return 1;
    }
    ComponentInstanceRecord rec;
    rec.typeId=L"tekla_haunch_40";
    rec.schemaVersion=1;
    rec.mainPart=s.col;
    rec.secondaryParts={s.beam};
    const auto type=ComponentRegistry::instance().findType(rec.typeId);
    rec.params=alignParamsToSpecs(type->params,{});
    auto result=mat.commit(prep,rec);
    ck(result.ok,"Document native component commit");
    if(!result.ok) {
        std::printf("Commit failed: %ls\n",result.error.c_str());
        return 1;
    }
    const auto ci=s.doc.indexOfComponentInstance(result.componentId);
    ck(ci.has_value(),"Haunch component instance indexed");
    if(ci) {
        const auto& saved=s.doc.componentInstances()[*ci];
        ck(saved.ownedPlateIds.size()==4,"4 owned physical plate entities");
        ck(saved.ownedBoltGroupIds.size()==2,"2 owned bolt groups");
        ck(saved.typeId==L"tekla_haunch_40","Native identity persisted");
        for(EntityId id:saved.ownedPlateIds) {
            const TopoDS_Shape* solid=s.store->find(id);
            ck(solid && !solid->IsNull() && shapeVolume(*solid)>0.0,
               "Native Haunch 40 plate has physical BRep");
        }
    }
    ck(s.doc.undo(),"Undo reverses one haunch creation");
    ck(s.doc.componentInstances().empty(),"Undo removes native component");
    ck(s.doc.redo(),"Redo restores haunch");
    ck(s.doc.indexOfComponentInstance(result.componentId).has_value(),
       "Redo restores same native component ID");
    std::printf("%d failure(s)\n",failures);
    return failures?1:0;
}
