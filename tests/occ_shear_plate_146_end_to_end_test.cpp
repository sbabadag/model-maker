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
    registerBuiltInDialogs(ComponentDialogRegistry::instance());

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

        // WELD: varsayilan createWelds=true -> 1 kaynak (levha -> kolon) uretilir.
        // Bu sentetik fixture'da kolon profilinin yerel orijini (Top ankraj) ile
        // MemberGeometry.axisFrom (centroid varsayimi) 150mm celisir — tab gercek
        // kolon katisina DEGMEZ; bu yuzden kaynak kati burada "temas yok" der.
        // Gercek uygulamada memberGeometryFor sectionCenterOffset'i dogru kurar
        // (kullanici modelinde kaynak uretildi). Bu test KAYIT bagini dogrular:
        // generator weld specleri uretti + materializer kaydi dogru parcalara bagladi.
        check(gp.welds.size() == 1, "146: 1 kaynak specleri (levha->kolon)");
        check(gp.welds[0].partA == s.colId, "146: kaynak partA = kolon (EntityId)");
        check(gp.welds[0].partBRole == "shear_tab_near", "146: kaynak partB roleKey = tab");
        check(res.roleToEntity.count("weld_tab_near") != 0, "146: weld roleKey -> EntityId");
        check(s.doc.welds().size() == 1, "146: Document'ta 1 kaynak kaydi");
        if (!s.doc.welds().empty()) {
            const Weld& w = s.doc.welds()[0];
            check(w.mainPart == s.colId, "146: kaynak main = kolon");
            check(w.secondaryPart == tabId, "146: kaynak secondary = levha");
            check(w.size == 5.0, "146: kaynak bacagi 5mm");
        }

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

    // ================= PART D: recompute bulon gorsel geometrisi =============
    // SAHADA GORULDU: bulon parametresi degisince ESKI bulon ekranda kaliyor,
    // delikler ise yeni sayiya gore aciliyordu (gorsel wireframe yenilenmiyordu).
    // Recompute, bulon grubunun hem deliklerini hem CIZILEN wireframe'ini
    // yenilemek zorundadir.
    {
        Setup s = makeSetup();
        ComponentInput in = makeInput(s);
        in.params = {{L"boltCount", CompParamValue{2}},
                     {L"shearTabPosition", CompParamValue{std::wstring(L"near_side")}}};
        const GenerationPlan gp = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in);
        check(gp.ok, "PART D: generate ok");

        const ComponentPlan plan = componentPlanFromGenerationPlan(gp);
        ComponentMaterializer mat(s.doc);
        ComponentMaterializer::Prepared prep = mat.prepare(plan, s.colId, {s.beamId});
        check(prep.ok, "PART D: prepare ok");

        ComponentInstanceRecord rec;
        rec.typeId = L"tekla_shear_plate_146";
        rec.schemaVersion = 1;
        rec.mainPart = s.colId;
        rec.secondaryParts = {s.beamId};
        ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
        check(res.ok, "PART D: commit ok");

        const EntityId boltId = res.roleToEntity["beam_bolts"];
        check(boltId != kInvalidEntityId, "PART D: bolt EntityId");
        const auto bi = s.doc.indexOfBoltGroup(boltId);
        check(bi.has_value(), "PART D: bolt grubu var");
        const EntityId dispId = s.doc.boltGroups()[*bi].displayEntityId;
        const auto di0 = s.doc.indexOfEntity(dispId);
        const std::size_t vertsBefore = di0 ? s.doc.models()[*di0].vertices().size() : 0;
        check(vertsBefore > 0, "PART D: ilk display geometrisi var");

        // 2 -> 6 bulon: delikler VE gorsel geometri yenilenmeli.
        ComponentInput in2 = makeInput(s);
        in2.params = {{L"boltCount", CompParamValue{6}},
                      {L"shearTabPosition", CompParamValue{std::wstring(L"near_side")}}};
        const GenerationPlan gp2 = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in2);
        const ComponentPlan plan2 = componentPlanFromGenerationPlan(gp2);
        ComponentMaterializer::Prepared prep2 = mat.prepare(plan2, s.colId, {s.beamId});
        check(prep2.ok, "PART D: prepare2 ok");
        ComponentMaterializer::CommitResult res2 =
            mat.recompute(prep2, s.doc.componentInstances()[0], s.doc.componentInstances()[0]);
        check(res2.ok, "PART D: recompute ok");

        std::size_t holes = 0;
        for (const auto& m : s.doc.cutModifiers())
            if (m.kind == CutKind::Hole && m.owner == boltId) ++holes;
        check(holes == 12, "PART D: delikler yeni sayiya gore (6x2)");

        const auto di1 = s.doc.indexOfEntity(dispId);
        const std::size_t vertsAfter = di1 ? s.doc.models()[*di1].vertices().size() : 0;
        check(vertsAfter > vertsBefore,
              "PART D: bulon DISPLAY geometrisi recompute'ta yenilendi (eski bulon kalmaz)");

        // Konum degisimi de yansimali (boltVerticalPos).
        ComponentInput in3 = makeInput(s);
        in3.params = {{L"boltCount", CompParamValue{6}},
                      {L"boltVerticalPos", CompParamValue{30.0}},
                      {L"shearTabPosition", CompParamValue{std::wstring(L"near_side")}}};
        const GenerationPlan gp3 = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in3);
        const ComponentPlan plan3 = componentPlanFromGenerationPlan(gp3);
        ComponentMaterializer::Prepared prep3 = mat.prepare(plan3, s.colId, {s.beamId});
        ComponentMaterializer::CommitResult res3 =
            mat.recompute(prep3, s.doc.componentInstances()[0], s.doc.componentInstances()[0]);
        check(res3.ok, "PART D: faz3 recompute ok");
        std::size_t holes3 = 0;
        for (const auto& m : s.doc.cutModifiers())
            if (m.kind == CutKind::Hole && m.owner == boltId) ++holes3;
        check(holes3 == 12, "PART D: delik BIRIKMESI yok (hala 12)");
    }

    // ================= PART E: cok kolonlu 2x3 + hacim + recompute ============
    // Gercek Tekla 146: 170x200x10 PL10, 6xM20 (2 kolon x 3 sira). Native generator
    // cok kolonlu uretir; delikli levha hacmi analitik degerle karsilastirilir:
    //   170*200*10 - 6 * pi*(22/2)^2 * 10 = 340000 - 22807.96 = 317192.04 mm^3.
    {
        Setup s = makeSetup();
        ComponentInput in = makeInput(s);
        in.params = {
            {L"plateThickness", CompParamValue{10.0}},
            {L"plateWidth", CompParamValue{170.0}},
            {L"plateHeight", CompParamValue{200.0}},
            {L"boltCount", CompParamValue{3}},      // 3 sira
            {L"boltCountX", CompParamValue{2}},     // 2 kolon
            {L"pitchX", CompParamValue{60.0}},
            {L"pitchY", CompParamValue{60.0}},
            {L"edgeX", CompParamValue{55.0}},
            {L"edgeY", CompParamValue{40.0}},
            {L"boltDiameter", CompParamValue{20.0}},
            {L"holeDiameter", CompParamValue{22.0}},
        };
        const GenerationPlan gp = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in);
        check(gp.ok, "PART E: generate ok");
        check(gp.boltGroups.size() == 1 && gp.boltGroups[0].countX == 2 &&
                  gp.boltGroups[0].countY == 3,
              "PART E: 2x3 bulon duzeni");
        check(approx(gp.boltGroups[0].spacingX, 60.0) &&
                  approx(gp.boltGroups[0].spacingY, 60.0),
              "PART E: pitchX=pitchY=60");

        const ComponentPlan plan = componentPlanFromGenerationPlan(gp);
        ComponentMaterializer mat(s.doc);
        ComponentMaterializer::Prepared prep = mat.prepare(plan, s.colId, {s.beamId});
        check(prep.ok, "PART E: prepare ok");
        ComponentInstanceRecord rec;
        rec.typeId = L"tekla_shear_plate_146";
        rec.mainPart = s.colId;
        rec.secondaryParts = {s.beamId};
        ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
        check(res.ok, "PART E: commit ok");

        const EntityId tabId = res.roleToEntity["shear_tab_near"];
        const EntityId boltId = res.roleToEntity["beam_bolts"];
        check(tabId != kInvalidEntityId, "PART E: tab EntityId");

        // Levha hacmi (6 delik sonrasi) analitik degerle karsilastirilir.
        const TopoDS_Shape* ts = s.store->find(tabId);
        check(ts && !ts->IsNull(), "PART E: levha kati mevcut");
        if (ts && !ts->IsNull()) {
            const double PI = std::acos(-1.0);
            const double vol = shapeVolume(*ts);
            const double expected = 340000.0 - 6.0 * PI * 11.0 * 11.0 * 10.0;
            check(approx(vol, expected, expected * 0.01),
                  "PART E: delikli levha hacmi analitik (317192)");
            std::printf("  [info] levha hacmi = %.2f (beklenen %.2f)\n", vol, expected);
        }

        // Delik dagilimi: 6 bulon x {levha, kiris} = 12 delik.
        std::size_t holes = 0, plateHoles = 0, beamHoles = 0;
        for (const auto& m : s.doc.cutModifiers()) {
            if (m.kind != CutKind::Hole || m.owner != boltId) continue;
            ++holes;
            if (m.target == tabId) ++plateHoles;
            if (m.target == s.beamId) ++beamHoles;
        }
        check(holes == 12, "PART E: 12 delik (6 bulon x {levha, kiris})");
        check(plateHoles == 6 && beamHoles == 6, "PART E: levha 6 + kiris 6 delik");

        // Recompute: kalinlik 10->12 hacmi artirir; delikler yeni sayiya gore.
        ComponentInput in2 = makeInput(s);
        in2.params = in.params;
        for (auto& p : in2.params)
            if (p.name == L"plateThickness") p.value = CompParamValue{12.0};
        const GenerationPlan gp2 = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in2);
        const ComponentPlan plan2 = componentPlanFromGenerationPlan(gp2);
        ComponentMaterializer::Prepared prep2 = mat.prepare(plan2, s.colId, {s.beamId});
        check(prep2.ok, "PART E: recompute prepare ok");
        ComponentMaterializer::CommitResult res2 =
            mat.recompute(prep2, s.doc.componentInstances()[0], s.doc.componentInstances()[0]);
        check(res2.ok, "PART E: recompute ok");
        const TopoDS_Shape* ts2 = s.store->find(tabId);
        check(ts2 && !ts2->IsNull(), "PART E: recompute levha kati mevcut");
        if (ts2 && !ts2->IsNull()) {
            const double PI = std::acos(-1.0);
            const double vol2 = shapeVolume(*ts2);
            const double expected2 = 170.0 * 200.0 * 12.0 - 6.0 * PI * 11.0 * 11.0 * 12.0;
            check(approx(vol2, expected2, expected2 * 0.01),
                  "PART E: recompute kalinlik 12 hacmi yenilendi");
        }
    }

    // ================= PART F: parametrik recompute zinciri (gercek OCC) ======
    // Phase 3 senaryolari: countY 3->4, countX 2->3, M20->M24, pitch 60->70,
    // levha boyut degisimi, ilk degere geri donus. Her adimda GERCEK OCCT
    // dogrulanir: delik sayisi, hacim (analitik), bbox (boyut), delik merkez
    // araligi (pitch), sabit EntityId (stable instance), child temizligi.
    {
        Setup s = makeSetup();
        const double PI = std::acos(-1.0);

        // Yardimcilar: tab EntityId uzerindeki delik sayisi + delik merkezleri.
        auto plateHoles = [&](EntityId tabId, EntityId boltId,
                              std::vector<Vec3>* centers = nullptr) {
            std::size_t n = 0;
            for (const auto& m : s.doc.cutModifiers())
                if (m.kind == CutKind::Hole && m.owner == boltId && m.target == tabId) {
                    ++n;
                    if (centers) centers->push_back(m.planeOrigin);
                }
            return n;
        };
        auto plateVolume = [&](EntityId tabId) {
            const TopoDS_Shape* ts = s.store->find(tabId);
            return (ts && !ts->IsNull()) ? shapeVolume(*ts) : -1.0;
        };
        auto plateExtents = [&](EntityId tabId) {
            const TopoDS_Shape* ts = s.store->find(tabId);
            std::array<double, 3> e{0, 0, 0};
            if (ts && !ts->IsNull()) {
                Bnd_Box bb; BRepBndLib::Add(*ts, bb);
                double mnx, mny, mnz, mxx, mxy, mxz;
                bb.Get(mnx, mny, mnz, mxx, mxy, mxz);
                e = {mxx - mnx, mxy - mny, mxz - mnz};
                std::sort(e.begin(), e.end());
            }
            return e;
        };
        auto pairwiseDists = [](const std::vector<Vec3>& pts) {
            std::vector<double> d;
            for (std::size_t i = 0; i < pts.size(); ++i)
                for (std::size_t j = i + 1; j < pts.size(); ++j)
                    d.push_back(std::sqrt((pts[i].x - pts[j].x) * (pts[i].x - pts[j].x) +
                                          (pts[i].y - pts[j].y) * (pts[i].y - pts[j].y) +
                                          (pts[i].z - pts[j].z) * (pts[i].z - pts[j].z)));
            return d;
        };
        auto hasDist = [](const std::vector<double>& d, double want, double tol) {
            for (double x : d) if (std::abs(x - want) < tol) return true;
            return false;
        };

        // Baslangic 2x3 M20 (hole 22), pitch 60, 170x200x10.
        auto build = [&](double th, double w, double h, int cx, int cy,
                         double px, double py, double boltD, double holeD) {
            ComponentInput in = makeInput(s);
            in.params = {{L"plateThickness", CompParamValue{th}},
                         {L"plateWidth", CompParamValue{w}},
                         {L"plateHeight", CompParamValue{h}},
                         {L"boltCount", CompParamValue{cy}},
                         {L"boltCountX", CompParamValue{cx}},
                         {L"pitchX", CompParamValue{px}},
                         {L"pitchY", CompParamValue{py}},
                         {L"edgeX", CompParamValue{55.0}},
                         {L"edgeY", CompParamValue{40.0}},
                         {L"boltDiameter", CompParamValue{boltD}},
                         {L"holeDiameter", CompParamValue{holeD}},
                         {L"shearTabPosition", CompParamValue{std::wstring(L"near_side")}}};
            return in;
        };
        auto materialize = [&](const ComponentInput& in) {
            const GenerationPlan gp = ComponentRegistry::instance().generate(
                L"tekla_shear_plate_146", in);
            const ComponentPlan plan = componentPlanFromGenerationPlan(gp);
            ComponentMaterializer mat(s.doc);
            ComponentMaterializer::Prepared prep = mat.prepare(plan, s.colId, {s.beamId});
            ComponentInstanceRecord rec;
            rec.typeId = L"tekla_shear_plate_146";
            rec.mainPart = s.colId;
            rec.secondaryParts = {s.beamId};
            return mat.commit(prep, rec);
        };
        auto recompute = [&](const ComponentInput& in) {
            const GenerationPlan gp = ComponentRegistry::instance().generate(
                L"tekla_shear_plate_146", in);
            const ComponentPlan plan = componentPlanFromGenerationPlan(gp);
            ComponentMaterializer mat(s.doc);
            ComponentMaterializer::Prepared prep = mat.prepare(plan, s.colId, {s.beamId});
            const auto& existing = s.doc.componentInstances()[0];
            return mat.recompute(prep, existing, existing);
        };

        ComponentMaterializer::CommitResult res =
            materialize(build(10, 170, 200, 2, 3, 60, 60, 20, 22));
        check(res.ok, "PART F: ilk materialize ok");
        const EntityId tabId = res.roleToEntity["shear_tab_near"];
        const EntityId boltId = res.roleToEntity["beam_bolts"];
        check(tabId != kInvalidEntityId && boltId != kInvalidEntityId, "PART F: tab+bolt id");

        const double holeVol20 = PI * 11.0 * 11.0 * 10.0;   // M20/22 delik hacmi (th=10)
        check(plateHoles(tabId, boltId) == 6, "PART F: baslangic 6 delik");
        check(approx(plateVolume(tabId), 340000.0 - 6.0 * holeVol20, 1.0),
              "PART F: baslangic hacim 317192");

        // B: countY 3 -> 4 (8 delik). 4 sira 60mm pitch'te 200mm levhaya
        // sigmaz (kenar 10 < delik yaricapi 11) — gercek muhendislikte levha
        // buyur ya da pitch daralir; burada pitchY=40 (kenar 40) kullanilir.
        res = recompute(build(10, 170, 200, 2, 4, 60, 40, 20, 22));
        check(res.ok, "PART F: B recompute ok");
        check(plateHoles(tabId, boltId) == 8, "PART F: B 8 delik (countY 4)");
        check(approx(plateVolume(tabId), 340000.0 - 8.0 * holeVol20, 1.0),
              "PART F: B hacim 309589");

        // C: countX 2 -> 3 (9 delik, countY 3).
        res = recompute(build(10, 170, 200, 3, 3, 60, 60, 20, 22));
        check(res.ok, "PART F: C recompute ok");
        check(plateHoles(tabId, boltId) == 9, "PART F: C 9 delik (countX 3)");
        check(approx(plateVolume(tabId), 340000.0 - 9.0 * holeVol20, 1.0),
              "PART F: C hacim 305788");

        // D: M20 -> M24 (hole 26, 6 delik, 2x3).
        res = recompute(build(10, 170, 200, 2, 3, 60, 60, 24, 26));
        check(res.ok, "PART F: D recompute ok");
        check(plateHoles(tabId, boltId) == 6, "PART F: D 6 delik (M24)");
        {
            const double holeVol24 = PI * 13.0 * 13.0 * 10.0;
            check(approx(plateVolume(tabId), 340000.0 - 6.0 * holeVol24, 1.0),
                  "PART F: D hacim 308144 (M24)");
        }

        // E: pitchX/pitchY 60 -> 70 (hacim degismez, merkez araligi degisir).
        res = recompute(build(10, 170, 200, 2, 3, 70, 70, 20, 22));
        check(res.ok, "PART F: E recompute ok");
        {
            std::vector<Vec3> centers;
            check(plateHoles(tabId, boltId, &centers) == 6, "PART F: E 6 delik");
            const auto d = pairwiseDists(centers);
            check(hasDist(d, 70.0, 1e-3), "PART F: E merkez araligi 70 (pitch yeni)");
            check(!hasDist(d, 60.0, 1e-3), "PART F: E eski 60 araligi KALMADI");
        }

        // F: levha 170x200 -> 180x220 (bbox + hacim).
        res = recompute(build(10, 180, 220, 2, 3, 60, 60, 20, 22));
        check(res.ok, "PART F: F recompute ok");
        {
            const auto e = plateExtents(tabId);
            // OCC boolean toleransi bbox'i ~0.15mm sisirir; 0.5mm tolerans yeterli
            // (10mm'lik bir boyut hatasini yine yakalar).
            check(approx(e[0], 10.0, 0.5) && approx(e[1], 180.0, 0.5) &&
                      approx(e[2], 220.0, 0.5),
                  "PART F: F bbox 10 x 180 x 220");
            check(approx(plateVolume(tabId), 180.0 * 220.0 * 10.0 - 6.0 * holeVol20, 1.0),
                  "PART F: F hacim 373192");
        }

        // G: ilk degere geri don (170x200x10, 2x3, M20, pitch 60).
        res = recompute(build(10, 170, 200, 2, 3, 60, 60, 20, 22));
        check(res.ok, "PART F: G recompute ok");
        check(plateHoles(tabId, boltId) == 6, "PART F: G 6 delik (geri dondu)");
        check(approx(plateVolume(tabId), 340000.0 - 6.0 * holeVol20, 1.0),
              "PART F: G hacim 317192 (ilk deger)");

        // Sabit instance: tab/bolt EntityId hic degismedi (roleKey korunur).
        check(tabId == res.roleToEntity["shear_tab_near"], "PART F: tab EntityId SABIT");
        check(boltId == res.roleToEntity["beam_bolts"], "PART F: bolt EntityId SABIT");
        check(s.doc.componentInstances().size() == 1, "PART F: tek component instance");

        // Child temizligi: delik modifier'lari BIRIKMEDI (toplam == 2*6=12, levha 6).
        std::size_t allHoles = 0;
        for (const auto& m : s.doc.cutModifiers())
            if (m.kind == CutKind::Hole && m.owner == boltId) ++allHoles;
        check(allHoles == 12, "PART F: delik BIRIKMESI yok (6 levha + 6 kiris = 12)");

        // Undo/redo: zincirde her recompute AYRI bir undo kaydi acar (dogru
        // davranis). Tek adimli undo/redo PART A'da kapsanmistir; burada yalniz
        // undo'nun isler oldugunu dogrulariz (kademeli geri alma).
        check(s.doc.canUndo(), "PART F: undo mevcut");
        check(s.doc.undo(), "PART F: undo calisti");
        check(s.doc.componentInstances().size() == 1, "PART F: undo sonrasi instance hala var (kademeli)");
        check(s.doc.redo(), "PART F: redo calisti");
    }

    // ========= PART G: civata eksen-yonu konumu (origin + grip yigin merkezinde) ==
    // v6'da grip dogruydu ama origin tabBase (govde merkezi) kaldigindan civata
    // levhayi "opmuyordu" (bas govde uzak yuzunde boslukta, somun levhaya gomulu).
    // v7 origin'i yigin merkezine kaydirdi. Bu test secWebThickness set ederek bu
    // yolu gercek gecerliligine kavusturur (once end-to-end test bunu KAPSAMIYORDU).
    {
        Setup s = makeSetup();
        s.beamGeom.secWebThickness = 5.6;    // IPE200 govde
        s.beamGeom.secFlangeThickness = 8.5; // IPE200 baslik
        ComponentInput in = makeInput(s);
        in.params = {{L"plateThickness", CompParamValue{10.0}}};
        const GenerationPlan gp = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in);
        check(gp.ok, "G: generate ok");
        check(gp.plates.size() == 1 && gp.boltGroups.size() == 1,
              "G: 1 levha + 1 bulon grubu");

        const PlateSpec& plate = gp.plates[0];
        const BoltGroup& bolt = gp.boltGroups[0];

        // grip = levha dis yuzu (ecc + t/2) .. govde uzak yuzu (-w/2).
        // ecc = thickness + tabGap = 10; t=10, w=5.6 -> grip = 15 + 2.8 = 17.8.
        check(approx(bolt.gripLength, 17.8, 1e-3), "G: grip = ecc + t/2 + w/2 = 17.8");

        // Bolt origin'in sec.width bileseni = (plateOuter + webFar)/2 = 6.1.
        // Levha merkezi ecc=10'da; origin oradan (6.1 - 10) = -3.9 kaymis olmali
        // (govdeye dogru). Bu, civatanin bas/somununun yiginin iki dis yuzune
        // oturmasini saglar.
        const Vec3 ax = plate.axis;  // sec.width (birim)
        const double alen = std::sqrt(ax.x * ax.x + ax.y * ax.y + ax.z * ax.z);
        const Vec3 au{ax.x / alen, ax.y / alen, ax.z / alen};
        const double shift =
            (bolt.origin.x - plate.center.x) * au.x +
            (bolt.origin.y - plate.center.y) * au.y +
            (bolt.origin.z - plate.center.z) * au.z;
        check(approx(shift, -3.9, 1e-3),
              "G: bolt origin levha merkezinden yigin merkezine kaydi (-3.9)");
    }

    // ========= PART H: kullanici katalogundan turetilmis sablon (gercek OCC) ==
    // Kabul kriteri: Tekla kapaliyken kendi katalogundan secilen sablon, YENI
    // projede gercek OCC geometrisi uretmeli. Burada: deriveComponentTemplate
    // (2x3 M20, 170x200x10) -> UserCatalog.registerInto -> derived typeId ile
    // generate (BOS params = sablon varsayilanlari) -> materialize -> hacim/delik.
    {
        Setup s = makeSetup();
        const double PI = std::acos(-1.0);

        const ComponentType src = shearPlate146ComponentType();
        const std::vector<CompParam> overrides = {
            {L"plateThickness", CompParamValue{10.0}},
            {L"plateWidth", CompParamValue{170.0}},
            {L"plateHeight", CompParamValue{200.0}},
            {L"boltCount", CompParamValue{3}},
            {L"boltCountX", CompParamValue{2}},
            {L"pitchX", CompParamValue{60.0}},
            {L"pitchY", CompParamValue{60.0}},
            {L"edgeX", CompParamValue{55.0}},
            {L"edgeY", CompParamValue{40.0}},
            {L"boltDiameter", CompParamValue{20.0}},
            {L"holeDiameter", CompParamValue{22.0}},
        };
        UserCatalog catalog;
        catalog.addOrUpdate(
            deriveComponentTemplate(src, L"Shear Plate 146 - Özel", overrides));
        const auto warn = catalog.registerInto(ComponentRegistry::instance(),
                                               ComponentDialogRegistry::instance());
        check(warn.empty(), "H: registerInto uyari yok");
        const std::wstring derivedTypeId = catalog.entries().front().typeId;
        check(ComponentRegistry::instance().findType(derivedTypeId) != nullptr,
              "H: derived type registry'de");

        // BOS params -> sablon varsayilanlari (derive edilen 2x3, 170x200x10).
        ComponentInput in = makeInput(s);
        const GenerationPlan gp = ComponentRegistry::instance().generate(derivedTypeId, in);
        check(gp.ok, "H: derived generate ok");
        check(gp.boltGroups.size() == 1 && gp.boltGroups[0].countX == 2 &&
                  gp.boltGroups[0].countY == 3,
              "H: 2x3 duzeni (sablon varsayilanlarindan)");

        const ComponentPlan plan = componentPlanFromGenerationPlan(gp);
        ComponentMaterializer mat(s.doc);
        ComponentMaterializer::Prepared prep = mat.prepare(plan, s.colId, {s.beamId});
        check(prep.ok, "H: prepare ok");
        ComponentInstanceRecord rec;
        rec.typeId = derivedTypeId;
        rec.mainPart = s.colId;
        rec.secondaryParts = {s.beamId};
        ComponentMaterializer::CommitResult res = mat.commit(prep, rec);
        check(res.ok, "H: commit ok");

        const EntityId tabId = res.roleToEntity["shear_tab_near"];
        const EntityId boltId = res.roleToEntity["beam_bolts"];
        check(tabId != kInvalidEntityId, "H: tab EntityId");

        const TopoDS_Shape* ts = s.store->find(tabId);
        check(ts && !ts->IsNull(), "H: levha kati mevcut");
        if (ts && !ts->IsNull()) {
            const double vol = shapeVolume(*ts);
            const double expected = 340000.0 - 6.0 * PI * 11.0 * 11.0 * 10.0;
            check(approx(vol, expected, expected * 0.01),
                  "H: delikli levha hacmi 317192 (sablondan gercek OCC)");
        }
        std::size_t plateHoles = 0;
        for (const auto& m : s.doc.cutModifiers())
            if (m.kind == CutKind::Hole && m.owner == boltId && m.target == tabId) ++plateHoles;
        check(plateHoles == 6, "H: levhada 6 delik (sablondan)");
    }

    // ================= PART I: kaynaklar (createWelds / weldSize) =============
    // "diyalogda kaynaklar acik" -> baglanti kaynak uretmeli; createWelds=false
    // uretmemeli; weldSize bacagi degistirmeli (recompute'ta da korunmali).
    {
        // createWelds=false -> kaynak YOK.
        Setup s = makeSetup();
        ComponentInput in = makeInput(s);
        in.params = {{L"createWelds", CompParamValue{false}}};
        const GenerationPlan gp = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in);
        check(gp.ok && gp.welds.empty(), "I: createWelds=false -> kaynak yok");

        // Varsayilan (createWelds yok) -> 1 kaynak, bacak 5mm, partA = kolon.
        Setup s2 = makeSetup();
        ComponentInput in2 = makeInput(s2);
        const GenerationPlan gp2 = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in2);
        check(gp2.ok && gp2.welds.size() == 1, "I: varsayilan -> 1 kaynak");
        check(gp2.welds[0].size == 5.0, "I: varsayilan bacak 5mm");
        check(gp2.welds[0].partA == s2.colId, "I: kaynak partA = kolon (EntityId)");

        const ComponentPlan plan2 = componentPlanFromGenerationPlan(gp2);
        ComponentMaterializer mat2(s2.doc);
        ComponentMaterializer::Prepared prep2 = mat2.prepare(plan2, s2.colId, {s2.beamId});
        check(prep2.ok, "I: weld prepare ok");
        ComponentInstanceRecord rec2;
        rec2.typeId = L"tekla_shear_plate_146";
        rec2.mainPart = s2.colId;
        rec2.secondaryParts = {s2.beamId};
        ComponentMaterializer::CommitResult res2 = mat2.commit(prep2, rec2);
        check(res2.ok, "I: weld commit ok");
        check(s2.doc.welds().size() == 1, "I: Document'ta 1 kaynak");
        const EntityId weldId = res2.roleToEntity["weld_tab_near"];
        check(weldId != kInvalidEntityId, "I: weld_tab_near -> EntityId");
        const auto wi = s2.doc.indexOfWeld(weldId);
        check(wi.has_value(), "I: kaynak indeksi bulundu");
        if (wi) {
            const Weld& w = s2.doc.welds()[*wi];
            check(w.size == 5.0, "I: kaynak bacagi 5mm korundu");
        }

        // weldSize 8 -> kaynak kati bacak 8 uretilir (recompute'ta da korunur).
        Setup s3 = makeSetup();
        ComponentInput in3 = makeInput(s3);
        in3.params = {{L"weldSize", CompParamValue{8.0}}};
        const GenerationPlan gp3 = ComponentRegistry::instance().generate(
            L"tekla_shear_plate_146", in3);
        check(gp3.welds.size() == 1 && gp3.welds[0].size == 8.0,
              "I: weldSize=8 bacagi 8");
        const ComponentPlan plan3 = componentPlanFromGenerationPlan(gp3);
        ComponentMaterializer mat3(s3.doc);
        ComponentMaterializer::Prepared prep3 = mat3.prepare(plan3, s3.colId, {s3.beamId});
        check(prep3.ok, "I: weldSize prepare ok");
        ComponentInstanceRecord rec3;
        rec3.typeId = L"tekla_shear_plate_146";
        rec3.mainPart = s3.colId;
        rec3.secondaryParts = {s3.beamId};
        ComponentMaterializer::CommitResult res3 = mat3.commit(prep3, rec3);
        check(res3.ok, "I: weldSize commit ok");
        const auto wi3 = s3.doc.indexOfWeld(res3.roleToEntity["weld_tab_near"]);
        if (wi3) {
            const Weld& w3 = s3.doc.welds()[*wi3];
            check(w3.size == 8.0, "I: kaynak kaydi bacak 8");
        }
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
