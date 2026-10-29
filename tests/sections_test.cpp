#include "model_maker/sections.hpp"
#include "model_maker/drawing_set.hpp"
#include <cmath>
#include <cstdio>
#include <set>

using namespace mm;
using namespace mm::drawing;
static int bad = 0;
static void check(bool p, const char* what) { std::printf("%s %s\n", p ? "PASS" : "FAIL", what); bad += !p; }
static EntityId box(Document& d, double x0, double x1, double y0, double y1, double z0, double z1, const char* profile = "") {
    std::vector<Vec3> vs;
    for (int i=0;i<8;++i) vs.push_back({(i&1)?x1:x0,(i&2)?y1:y0,(i&4)?z1:z0});
    std::vector<Edge> es;
    for (std::size_t i=0;i<8;++i) for (std::size_t b=1;b<8;b<<=1) if (!(i&b)) es.push_back({i,i|b});
    WireframeModel m(vs,es,{{0,2,6,4},{1,3,7,5},{0,1,5,4},{2,3,7,6},{0,1,3,2},{4,5,7,6}});
    EntityProperties p; p.profileName=profile;
    p.axisFromX=x0; p.axisFromY=(y0+y1)/2; p.axisFromZ=(z0+z1)/2;
    p.axisToX=x1; p.axisToY=p.axisFromY; p.axisToZ=p.axisFromZ;
    m.setProperties(p); d.addModel(std::move(m)); return d.modelId(d.models().size()-1);
}
int main() {
    Document doc;
    const auto main=box(doc,0,100,-10,10,-20,20,"IPE100");
    const auto plate=box(doc,30,40,-15,15,-25,25);
    const auto changed=box(doc,70,100,-12,12,-22,22,"IPE120");
    fab::FabricationInventory inv = fab::extractFabrication(doc,{main,plate,changed},main);
    fab::PlateFeature pf; pf.plate=plate; pf.localMin.x=30; pf.localMax.x=40; inv.plates.push_back(pf);
    BoltGroup bolt; bolt.parts={main,plate}; bolt.origin={35,0,0}; doc.addBoltGroup(bolt, WireframeModel{});
    auto requests=changePoints(doc,{main,plate,changed},inv);
    bool sorted=true, plateStart=false, plateEnd=false, boltCentre=false, profileChange=false;
    for (std::size_t i=0;i<requests.size();++i) {
        if (requests[i].member!=main) continue;
        if (i && requests[i-1].member==main && requests[i-1].t>=requests[i].t) sorted=false;
        plateStart |= std::fabs(requests[i].t-30)<0.01;
        plateEnd |= std::fabs(requests[i].t-40)<0.01;
        boltCentre |= std::fabs(requests[i].t-35)<0.01;
        profileChange |= std::fabs(requests[i].t-70)<0.01;
    }
    check(sorted && plateStart && plateEnd && boltCentre && profileChange,"ordered plate, bolt and profile changes");
    SectionRequest r1{main,10,{}, {}, main}, r2{main,20,{}, {}, main};
    auto s1=generateSectionView(doc,{main},r1,inv.datum);
    auto s2=generateSectionView(doc,{main},r2,inv.datum);
    check(!s1.segments.empty() && sectionSignature(s1)==sectionSignature(s2),"same physical cross-section has one signature");
    check(sectionLabel(0)=="A-A" && sectionLabel(1)=="B-B" && sectionLabel(26)=="AA-AA", "deterministic section labels");
    BoltGroup close; close.parts={main}; close.origin={35.8,0,0}; doc.addBoltGroup(close,WireframeModel{});
    auto merged=changePoints(doc,{main,plate,changed},inv);
    int near35=0; for (const auto& r:merged) if (r.member==main && std::fabs(r.t-35)<1.0) ++near35;
    check(near35==1,"change points within 1 mm merge");
    const auto remote=box(doc,55,60,200,220,-20,20);
    auto unrelated=changePoints(doc,{main,plate,changed,remote},inv);
    bool remoteBoundary=false;
    for (const auto& r:unrelated) if (r.member==main && (std::fabs(r.t-55)<0.1 || std::fabs(r.t-60)<0.1))
        remoteBoundary=true;
    check(!remoteBoundary,"unattached remote plate does not create a member section");
    Document steppedDoc;
    Document baseDoc;
    box(baseDoc,0,100,-10,10,-20,20,"IPE100");
    auto model=baseDoc.models().front();
    std::vector<Vec3> vertices=model.vertices();
    for (auto p:model.vertices()) { p.x=50; vertices.push_back(p); }
    std::vector<Edge> edges=model.edges();
    WireframeModel stepped(vertices,edges,model.faces());
    stepped.setProperties(model.properties());
    // Extra actual contour vertices at x=50 represent a member-internal transition.
    steppedDoc.addModel(std::move(stepped));
    const auto steppedId=steppedDoc.modelId(0);
    auto steps=changePoints(steppedDoc,{steppedId},fab::extractFabrication(steppedDoc,{steppedId},steppedId));
    bool stepAt50=false; for (const auto& r:steps) stepAt50 |= std::fabs(r.t-50)<0.01;
    check(stepAt50,"member internal profile transition is a change point");
    auto section=generateSectionView(doc,{main,plate,changed},requests.front(),inv.datum);
    check(section.kind==ViewKind::Section && section.scale==1.0 && !section.segments.empty(),"real 1:1 cross-section");
    DrawingOptions options; options.views={ViewKind::Front}; options.detailViews=false;
    MarkRegistry reg;
    auto built=buildSheet(doc,reg,main,options);
    check(built.ok,"sheet built");
    int count=0; bool measured=true, scale=true;
    for (const auto& v:built.sheet.views) if (v.kind==ViewKind::Section) {
        ++count; scale &= v.scale==1.0;
        for (const auto& d:built.sheet.dimensions) if (d.viewId==v.id)
            measured &= std::fabs(d.measured()-d.value)<1e-6;
    }
    check(count>0 && scale && measured,"sections retain 1:1 scale and measured dimensions");
    DrawingPreferences prefs; prefs.autoSheetSize=false; prefs.sheetSize=SheetSize::A4;
    MarkRegistry setReg;
    auto set=buildDrawingSet(doc,setReg,main,options,prefs);
    bool sectionScale=true;
    for (const auto& v:set.primary()->views) if (v.kind==ViewKind::Section) sectionScale &= v.scale==1.0;
    check(set.ok && set.document.sheets().size()==1 && sectionScale,"one sheet, independent section scale after layout");
    bool titleOffset=false, duplicateCount=false;
    for (const auto& v:set.primary()->views) if (v.kind==ViewKind::Section) {
        duplicateCount |= v.sectionPoints>1;
        for (const auto& text:set.primary()->texts) if (text.viewId==v.id && text.kind==TextKind::ViewTitle &&
            !text.lines.empty() && text.lines[0].find("1:1")!=std::string::npos &&
            text.lines[0].find("uye basindan")!=std::string::npos) titleOffset=true;
    }
    check(titleOffset && duplicateCount,"section title carries 1:1, measured offset and duplicate count");
    options.sectionViews=false;
    auto disabled=buildSheet(doc,reg,main,options);
    bool absent=true;
    for (const auto& v:disabled.sheet.views) absent &= v.kind!=ViewKind::Section;
    check(disabled.ok && absent,"disabled sections produce no views");
    DrawingDocument round; round.addSheet(std::move(built.sheet)); std::string err;
    DrawingDocument parsed;
    check(DrawingDocument::deserialize(round.serialize(),parsed,err) && parsed.serialize()==round.serialize(),"section JSON roundtrip");
    bool sourceRoundtrip=false;
    if (!parsed.sheets().empty())
        for (const auto& v:parsed.sheets().front().views)
            if (v.kind==ViewKind::Section)
                sourceRoundtrip |= v.sectionMember==main && v.sectionSource!=kInvalidEntityId &&
                    v.sectionT>=0 && v.sectionPoints>=1 && !v.sectionReason.empty();
    check(sourceRoundtrip,"section member, t, cause and source persist");
    return bad ? 1 : 0;
}
