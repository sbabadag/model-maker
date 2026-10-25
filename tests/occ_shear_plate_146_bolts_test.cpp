// TEKLA 146 BOLTS sekmesi — GERCEK OCCT hacim testleri.
//
// Yuvali/oversize/No-hole delikleri ve bulon boyu artisini ANALITIK hacimle
// dogrular (modifier sayisi dogru ama sekil/konum yanlis olabilir). Levha
// 120x240x10, M20 delik 22: yuvarlak delik alani = pi*r^2; stadyum yuva alani =
// pi*r^2 + 2r*L (L = duz kisim = yuva uzantisi).
//
// Derleme: ~/.hermes/cache/scratch/occbuild/build.sh <bu dosya> <cikti>

#include "model_maker/component_materializer.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_glyph.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"
#include "model_maker/user_catalog.hpp"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-3) { return std::abs(a - b) < tol; }
static SteelProfile makeProfile(const char* name, double h, double b, double tw, double tf) {
    SteelProfile p;
    p.name = name;
    p.height = h;
    p.width = b;
    p.plateThickness = tw;
    p.flangeThickness = tf;
    return p;
}

// Yeni belge + kolon + kiris kurar; profilleri ve id'leri dondurur.
struct Setup {
    Document doc;
    OccSolidStore* store{nullptr};
    EntityId colId{kInvalidEntityId};
    EntityId beamId{kInvalidEntityId};
    MemberGeometry colGeom;
    MemberGeometry beamGeom;
    double beamMinYBefore{0.0};
};

static Setup makeSetup() {
    Setup s;
    const SteelProfile heb300 = makeProfile("HEB300", 300.0, 300.0, 11.0, 19.0);
    const SteelProfile ipe200 = makeProfile("IPE200", 200.0, 100.0, 5.6, 8.5);

    const TopoDS_Shape colSolid = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0, {0, 1, 0},
                                                     SectionAnchor::Center);
    const TopoDS_Shape beamSolid = extrudeProfileSolid(ipe200, {0, 1000, 1500}, {0, 60, 1500}, 0.0, {0, 0, 1});

    s.doc.setOccStore(makeOccSolidStore());
    s.store = asSolidStore(&s.doc.occStore());

    WireframeModel colModel = shapeToWireframeWithFaces(colSolid, 0.15);
    {
        EntityProperties p = colModel.properties();
        p.profileName = "HEB300";
        p.axisFromZ = 0; p.axisToZ = 3000;
        p.sectionNormalY = 1;
        colModel.setProperties(std::move(p));
    }
    s.doc.addModel(std::move(colModel));
    s.colId = s.doc.modelId(0);
    s.store->set(s.colId, colSolid);
    s.store->setBaseVersion(s.colId, s.store->activeVersion(s.colId));

    WireframeModel beamModel = shapeToWireframeWithFaces(beamSolid, 0.15);
    {
        EntityProperties p = beamModel.properties();
        p.profileName = "IPE200";
        p.axisFromY = 1000; p.axisFromZ = 1500;
        p.axisToY = 60; p.axisToZ = 1500;
        p.sectionNormalZ = 1;
        beamModel.setProperties(std::move(p));
    }
    s.doc.addModel(std::move(beamModel));
    s.beamId = s.doc.modelId(1);
    s.store->set(s.beamId, beamSolid);
    s.store->setBaseVersion(s.beamId, s.store->activeVersion(s.beamId));

    // Kiris bbox Y-min (kesim oncesi).
    Bnd_Box bb;
    BRepBndLib::Add(beamSolid, bb);
    double _mnx, _mny, _mnz, _mxx, _mxy, _mxz;
    bb.Get(_mnx, _mny, _mnz, _mxx, _mxy, _mxz);
    s.beamMinYBefore = _mny;

    s.colGeom.id = s.colId;
    s.colGeom.axisFrom = {0, 0, 0};
    s.colGeom.axisTo = {0, 0, 3000};
    s.colGeom.secWidth = 300.0;
    s.colGeom.secHeight = 300.0;
    // sectionCenterOffset = 0 (merkez ankraj) — kati da MERKEZ ankrajli cizilir
    // (makeSetup icinde SectionAnchor::Center). Boylece tab'in temas noktasi
    // gercek kolon yuzune duser ve kaynak kati uretilir.

    s.beamGeom.id = s.beamId;
    s.beamGeom.axisFrom = {0, 1000, 1500};
    s.beamGeom.axisTo = {0, 60, 1500};
    s.beamGeom.secWidth = 100.0;
    s.beamGeom.secHeight = 200.0;
    return s;
}

static ComponentInput makeInput(const Setup& s) {
    ComponentInput in;
    in.main = s.colGeom;
    in.secondary = {s.beamGeom};
    return in;
}


struct CaseOut {
    bool ok{false};
    double plateVol{0.0};
    double boltExtentX{0.0};
    std::size_t holeMods{0};
    std::size_t plateHoleMods{0};
};

static CaseOut runCase(const std::vector<CompParam>& params) {
    CaseOut out;
    Setup s = makeSetup();
    ComponentInput in = makeInput(s);
    in.params = params;
    const GenerationPlan gp = ComponentRegistry::instance().generate(L"tekla_shear_plate_146", in);
    if (!gp.ok) return out;
    const ComponentPlan plan = componentPlanFromGenerationPlan(gp);
    if (!plan.ok) return out;
    ComponentMaterializer mat(s.doc);
    ComponentMaterializer::Prepared prep = mat.prepare(plan, s.colId, {s.beamId});
    if (!prep.ok) return out;
    ComponentInstanceRecord rec;
    rec.typeId = L"tekla_shear_plate_146";
    rec.schemaVersion = 1;
    rec.mainPart = s.colId;
    rec.secondaryParts = {s.beamId};
    ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
    if (!res.ok) return out;
    const EntityId tabId = res.roleToEntity["shear_tab_near"];
    const TopoDS_Shape* ts = s.store->find(tabId);
    if (ts && !ts->IsNull()) out.plateVol = shapeVolume(*ts);
    for (const auto& m : s.doc.cutModifiers())
        if (m.kind == CutKind::Hole) {
            ++out.holeMods;
            if (m.target == tabId) ++out.plateHoleMods;
        }
    const EntityId boltId = res.roleToEntity["beam_bolts"];
    for (const auto& bg : s.doc.boltGroups()) {
        if (bg.id != boltId) continue;
        const TopoDS_Shape* bs = s.store->find(bg.displayEntityId);
        if (bs && !bs->IsNull()) {
            Bnd_Box bb;
            BRepBndLib::Add(*bs, bb);
            double a, b, c, d, e, f;
            bb.Get(a, b, c, d, e, f);
            out.boltExtentX = d - a;
        }
    }
    out.ok = true;
    return out;
}

static CompParam S(const wchar_t* n, const wchar_t* v) {
    return CompParam{n, CompParamValue{std::wstring(v)}};
}
static CompParam D(const wchar_t* n, double v) { return CompParam{n, CompParamValue{v}}; }
static CompParam I(const wchar_t* n, int v) { return CompParam{n, CompParamValue{v}}; }

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    registerBuiltInDialogs(ComponentDialogRegistry::instance());
    const double kPi = 3.14159265358979323846;
    const double plateSolid = 120.0 * 240.0 * 10.0;
    const double roundHole = kPi * 11.0 * 11.0 * 10.0;

    const CaseOut base = runCase({});
    check(base.ok, "A: varsayilan 146 olustu");
    std::printf("  A plateVol=%.1f beklenen=%.1f boltExtentX=%.2f\n", base.plateVol,
                plateSolid - 4.0 * roundHole, base.boltExtentX);
    check(approx(base.plateVol, plateSolid - 4.0 * roundHole, 1.0),
          "A: levha hacmi = 288000 - 4 yuvarlak delik (M20/22)");

    const CaseOut slot = runCase({S(L"holeType", L"Slotted"), D(L"slotV", 10.0)});
    check(slot.ok, "B: Slotted olustu");
    const double slotVol = (kPi * 11.0 * 11.0 + 22.0 * 10.0) * 10.0;
    std::printf("  B plateVol=%.1f beklenen=%.1f\n", slot.plateVol, plateSolid - 4.0 * slotVol);
    check(approx(slot.plateVol, plateSolid - 4.0 * slotVol, 1.0),
          "B: levha hacmi = 288000 - 4 yuvali delik (analitik stadyum)");
    check(slot.plateVol < base.plateVol - 100.0, "B: yuvali levhadan daha cok malzeme alinmis");

    const CaseOut slotR = runCase({S(L"holeType", L"Slotted"), D(L"slotV", 10.0), S(L"rotateSlots", L"Yes")});
    check(slotR.ok && approx(slotR.plateVol, plateSolid - 4.0 * slotVol, 1.0),
          "B2: Rotate slots ayni alan (yatay yuva)");

    const CaseOut slotBeam = runCase({S(L"holeType", L"Slotted"), D(L"slotV", 10.0), S(L"slotsIn", L"Beam")});
    check(slotBeam.ok && approx(slotBeam.plateVol, base.plateVol, 1.0),
          "B3: Slots in Beam: levha deligi yuvarlak kalir");

    const CaseOut over = runCase({S(L"holeType", L"Oversized"), D(L"slotH", 4.0)});
    check(over.ok && approx(over.plateVol, plateSolid - 4.0 * kPi * 13.0 * 13.0 * 10.0, 1.0),
          "C: Oversized (22+4=26) levha hacmi");

    const CaseOut none = runCase({S(L"holeType", L"No hole")});
    check(none.ok && approx(none.plateVol, plateSolid, 1.0) && none.holeMods == 0,
          "D: No hole: levha tam, hole modifier yok");

    const CaseOut tol = runCase({D(L"tolerance", 4.0)});
    check(tol.ok && approx(tol.plateVol, plateSolid - 4.0 * kPi * 12.0 * 12.0 * 10.0, 1.0),
          "E: tolerance 4 -> delik 24");

    const CaseOut stag = runCase({S(L"boltStagger", L"Staggered type 1"), I(L"boltCountX", 3),
                                  D(L"pitchX", 40.0), I(L"boltCount", 3)});
    std::printf("  F plateHoleMods=%zu plateVol=%.1f beklenen=%.1f\n", stag.plateHoleMods,
                stag.plateVol, plateSolid - 7.0 * roundHole);
    check(stag.ok && stag.plateHoleMods == 7 &&
              approx(stag.plateVol, plateSolid - 7.0 * roundHole, 1.0),
          "F: Staggered tip 1: levhada 7 delik, hacim dogru");

    const CaseOut lenInc = runCase({D(L"boltLengthIncrease", 10.0)});
    std::printf("  G extent %.2f -> %.2f\n", base.boltExtentX, lenInc.boltExtentX);
    check(lenInc.ok && base.boltExtentX > 0.0 && approx(lenInc.boltExtentX - base.boltExtentX, 10.0, 0.05),
          "G: boltLengthIncrease 10 -> bulon 10 mm uzar");

    std::printf("%s (%d failure)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
