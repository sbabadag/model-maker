// TEKLA 146 (Shear plate simple) UCTAN UCA (gercek OCCT kabul testi).
//
// Registry (generateShearPlateSimple146) -> ComponentPlan -> ComponentMaterializer
// -> Document/OCCT hatti ile 146'nin YENI yuzeyi dogrulanir:
//   - kiris ucu KARE kesimi (beam end cut): PlaneCut kiris solidini GERCEKTEN
//     keser (kiris kolona gomulmez; bbox Y-min yuz+gap'e cekilir).
//   - cift tab (near_and_far): 2 levha + bulon grubu 3 parcayi deler (12 delik).
//   - stiffener + backing bar: ek levhalar uretilir.
//   - undo/redo tek adimda geri alir; roleKey -> EntityId korunur.
//
// Derleme (Linux + OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_shear_plate_146_end_to_end_test.cpp \
//       src/component_materializer.cpp src/component_framework.cpp \
//       src/component_engine.cpp src/component_dialog.cpp src/document.cpp src/mmw8_codec.cpp \
//       src/end_plate.cpp src/bolt.cpp src/cut_modifier.cpp src/geometry.cpp \
//       src/profile_database.cpp src/reference_geometry.cpp \
//       src/constraint_system.cpp src/contact_resolver.cpp \
//       src/geometry_reference_resolver.cpp src/magnetic_frame.cpp \
//       src/component_recompute.cpp src/component.cpp src/assembly.cpp \
//       src/camera.cpp src/occ_contact_resolver.cpp src/occ_solid_store.cpp \
//       src/occ_brep_serialize.cpp src/occ_geometry.cpp src/bolt_geometry.cpp \
//       src/end_plate_geometry.cpp src/sha256.cpp src/grid.cpp \
//       src/occ_store_null.cpp src/component_glyph.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase -lTKDESTEP \
//       -o occ_shear_plate_146_end_to_end_test && ./occ_shear_plate_146_end_to_end_test

#include "model_maker/component_materializer.hpp"
#include "model_maker/component_framework.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_glyph.hpp"
#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>

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

    const TopoDS_Shape colSolid = extrudeProfileSolid(heb300, {0, 0, 0}, {0, 0, 3000}, 0.0, {0, 1, 0});
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

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    // ================= PART A: varsayilan 146 + kiris ucu kesimi ==============
    {
        Setup s = makeSetup();
        ComponentInput in = makeInput(s);
        const GenerationPlan gp = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in);
        check(gp.ok, "146 generate ok");
        check(gp.plates.size() == 1, "146: 1 tab (near)");
        check(gp.boltGroups.size() == 1, "146: 1 bulon grubu");
        check(gp.cuts.size() == 1, "146: 1 kiris ucu kesimi");

        const ComponentPlan plan = componentPlanFromGenerationPlan(gp);
        check(plan.ok, "ComponentPlan ok");

        ComponentMaterializer mat(s.doc);
        ComponentMaterializer::Prepared prep = mat.prepare(plan, s.colId, {s.beamId});
        check(prep.ok, "prepare ok");

        ComponentInstanceRecord rec;
        rec.typeId = L"tekla_shear_plate_146";
        rec.schemaVersion = 1;
        rec.mainPart = s.colId;
        rec.secondaryParts = {s.beamId};
        ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
        check(res.ok, "commit ok");

        const EntityId tabId = res.roleToEntity["shear_tab_near"];
        const EntityId boltId = res.roleToEntity["beam_bolts"];
        check(tabId != kInvalidEntityId, "shear_tab_near -> EntityId");
        check(boltId != kInvalidEntityId, "beam_bolts -> EntityId");

        // Levha kati gercek OCCT.
        const TopoDS_Shape* ts = s.store->find(tabId);
        check(ts && !ts->IsNull() && shapeVolume(*ts) > 0.0, "146 levha OCCT kati uretildi");

        // Kiris ucu kesimi: PlaneCut hedef kiris, owner component.
        bool endCutFound = false;
        for (const auto& m : s.doc.cutModifiers()) {
            if (m.kind == CutKind::PlaneCut && m.target == s.beamId) {
                endCutFound = true;
                check(m.owner == s.doc.componentInstances()[0].id,
                      "kiris ucu kesimi component-owned");
            }
        }
        check(endCutFound, "kiris ucu PlaneCut kayitli");

        // Kiris GERCEKTEN kesildi: bbox Y-min yuz+gap'e cekildi (y=60 -> ~170).
        const TopoDS_Shape* bs = s.store->find(s.beamId);
        check(bs && !bs->IsNull(), "kiris kati mevcut (kesim sonrasi)");
        if (bs && !bs->IsNull()) {
            Bnd_Box bb;
            BRepBndLib::Add(*bs, bb);
            double mnx, mny, mnz, mxx, mxy, mxz;
            bb.Get(mnx, mny, mnz, mxx, mxy, mxz);
            check(mny > s.beamMinYBefore + 50.0,
                  "kiris ucu kesildi (bbox Y-min ilerledi)");
            check(approx(mny, 170.0, 2.0), "kiris ucu yuz+gap=170 civarinda");
        }

        // Delikler: 4 bulon x {levha, kiris} = 8 delik (kolon delinmez).
        std::size_t holes = 0, beamHoles = 0, colHoles = 0, plateHoles = 0;
        for (const auto& m : s.doc.cutModifiers()) {
            if (m.kind != CutKind::Hole || m.owner != boltId) continue;
            ++holes;
            if (m.target == s.beamId) ++beamHoles;
            if (m.target == s.colId) ++colHoles;
            if (m.target == tabId) ++plateHoles;
        }
        check(holes == 8, "8 delik (4 bulon x {levha, kiris})");
        check(beamHoles == 4 && plateHoles == 4 && colHoles == 0,
              "delik dagilimi: kiris 4, levha 4, kolon 0");

        // Undo/redo.
        check(s.doc.undo(), "undo component geri aldi");
        check(s.doc.componentInstances().empty(), "undo: instance silindi");
        check(s.doc.redo(), "redo geri getirdi");
        check(s.doc.componentInstances().size() == 1, "redo: instance geri geldi");
    }

    // ================= PART B: cift tab (near_and_far) ========================
    {
        Setup s = makeSetup();
        ComponentInput in = makeInput(s);
        in.params = {{L"shearTabPosition", CompParamValue{std::wstring(L"near_and_far")}}};
        const GenerationPlan gp = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in);
        check(gp.ok && gp.plates.size() == 2, "cift tab: 2 levha");

        const ComponentPlan plan = componentPlanFromGenerationPlan(gp);
        ComponentMaterializer mat(s.doc);
        ComponentMaterializer::Prepared prep = mat.prepare(plan, s.colId, {s.beamId});
        check(prep.ok, "cift tab prepare ok");

        ComponentInstanceRecord rec;
        rec.typeId = L"tekla_shear_plate_146";
        rec.mainPart = s.colId;
        rec.secondaryParts = {s.beamId};
        ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
        check(res.ok, "cift tab commit ok");

        const EntityId nearId = res.roleToEntity["shear_tab_near"];
        const EntityId farId = res.roleToEntity["shear_tab_far"];
        const EntityId boltId = res.roleToEntity["beam_bolts"];
        check(nearId != kInvalidEntityId && farId != kInvalidEntityId,
              "cift tab: iki levha EntityId");
        check(nearId != farId, "cift tab: ayri EntityId'ler");

        // Bulon grubu 3 parcayi deler: {near tab, far tab, kiris} -> 12 delik.
        std::size_t holes = 0, nearHoles = 0, farHoles = 0, beamHoles = 0, colHoles = 0;
        for (const auto& m : s.doc.cutModifiers()) {
            if (m.kind != CutKind::Hole || m.owner != boltId) continue;
            ++holes;
            if (m.target == nearId) ++nearHoles;
            if (m.target == farId) ++farHoles;
            if (m.target == s.beamId) ++beamHoles;
            if (m.target == s.colId) ++colHoles;
        }
        check(holes == 12, "cift tab: 12 delik (4 bulon x 3 parca)");
        check(nearHoles == 4 && farHoles == 4 && beamHoles == 4 && colHoles == 0,
              "cift tab delik dagilimi: near 4, far 4, kiris 4, kolon 0");
    }

    // ================= PART C: stiffener + backing bar ========================
    {
        Setup s = makeSetup();
        ComponentInput in = makeInput(s);
        in.params = {{L"stiffenerCreation", CompParamValue{std::wstring(L"full")}},
                     {L"backingBarT", CompParamValue{8.0}}};
        const GenerationPlan gp = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in);
        check(gp.ok && gp.plates.size() == 3, "stiffener+backing: 3 levha");

        const ComponentPlan plan = componentPlanFromGenerationPlan(gp);
        ComponentMaterializer mat(s.doc);
        ComponentMaterializer::Prepared prep = mat.prepare(plan, s.colId, {s.beamId});
        check(prep.ok, "stiffener+backing prepare ok");

        ComponentInstanceRecord rec;
        rec.typeId = L"tekla_shear_plate_146";
        rec.mainPart = s.colId;
        rec.secondaryParts = {s.beamId};
        ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
        check(res.ok, "stiffener+backing commit ok");

        check(res.roleToEntity["stiffener"] != kInvalidEntityId, "stiffener EntityId");
        check(res.roleToEntity["backing_bar"] != kInvalidEntityId, "backing bar EntityId");
        check(s.doc.componentInstances()[0].ownedPlateIds.size() == 3,
              "3 owned levha (tab + stiffener + backing bar)");
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
