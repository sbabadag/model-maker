// UNIVERSAL COMPONENT FRAMEWORK — cekirdek test (OCC'siz, Linux'ta kosar).
//
// ComponentBuilder + ComponentRelationshipGraph + ComponentObjectSpec:
// roleKey tekilligi/gecerliligi, fluent API, iliski dogrulama (unresolved /
// conflict / cyclic), ve SizeFromParameter'in parametre hedefi serbestligi.

#include "model_maker/component_framework.hpp"

#include <cstdio>
#include <string>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

int main() {
    // --- roleKey dogrulama ---------------------------------------------------
    check(isValidRoleKey("main_plate"), "isValidRoleKey main_plate");
    check(isValidRoleKey("beam_bolts_2"), "isValidRoleKey beam_bolts_2");
    check(!isValidRoleKey(""), "isValidRoleKey bos -> false");
    check(!isValidRoleKey("bad key"), "isValidRoleKey bosluk -> false");
    check(!isValidRoleKey("a-b"), "isValidRoleKey tire -> false");

    // --- iliski aileleri -----------------------------------------------------
    check(isGeometricRelationship(RelationshipKind::FaceContact), "FaceContact geometric");
    check(!isGeometricRelationship(RelationshipKind::SizeFromMember), "SizeFromMember geometric DEGIL");
    check(isParametricRelationship(RelationshipKind::SizeFromMember), "SizeFromMember parametric");
    check(isParametricRelationship(RelationshipKind::SizeFromParameter), "SizeFromParameter parametric");
    check(isDependencyRelationship(RelationshipKind::AttachedTo), "AttachedTo dependency");
    check(isDependencyRelationship(RelationshipKind::DependsOn), "DependsOn dependency");
    check(!isDependencyRelationship(RelationshipKind::Align), "Align dependency degil");
    check(isOwnershipRelationship(RelationshipKind::OwnedBy), "OwnedBy ownership");
    check(isOwnershipRelationship(RelationshipKind::Cuts), "Cuts ownership");
    check(isOwnershipRelationship(RelationshipKind::Welds), "Welds ownership");
    check(!isOwnershipRelationship(RelationshipKind::Offset), "Offset ownership degil");

    // --- Builder: mutlu yol (Fin Plate benzeri) ------------------------------
    {
        PlateSpec plate;
        plate.width = 120.0;
        plate.height = 240.0;
        plate.thickness = 10.0;
        plate.name = L"Fin Levhasi";

        BoltGroup bolts;
        bolts.countY = 2;

        WeldSpec weld;
        weld.throat = 6.0;

        ComponentBuilder b;
        b.plate("fin_plate", plate)
            .boltGroup("beam_bolts", bolts)
            .weld("fin_weld", weld)
            .align("fin_plate", "secondary_0")
            .faceContact("fin_plate", "main")
            .sizeFromMember("fin_plate", "secondary_0")
            .attachedTo("beam_bolts", "fin_plate")
            .ownedBy("fin_plate", "component");

        const auto r = b.build();
        check(r.ok, "builder mutlu yol ok");
        check(r.objects.size() == 3, "3 nesne");
        check(r.objects[0].roleKey == "fin_plate", "roleKey fin_plate");
        check(r.objects[1].roleKey == "beam_bolts", "roleKey beam_bolts");
        check(r.objects[2].roleKey == "fin_weld", "roleKey fin_weld");
        check(r.objects[0].plate.has_value(), "plate payload dolu");
        check(r.objects[1].boltGroup.has_value(), "boltGroup payload dolu");
        check(r.objects[2].weld.has_value(), "weld payload dolu");
        check(r.relationships.count() == 5, "5 iliski");
    }

    // --- Builder: tekrarlanan roleKey -> fail-closed -------------------------
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("dup", p).plate("dup", p);
        const auto r = b.build();
        check(!r.ok, "tekrarlanan roleKey -> ok=false");
    }

    // --- Builder: gecersiz roleKey -> fail-closed ----------------------------
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("bad key", p);
        const auto r = b.build();
        check(!r.ok, "gecersiz roleKey -> ok=false");
    }

    // --- Grafik: unresolved referans -----------------------------------------
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("fin_plate", p).faceContact("fin_plate", "yok_parca");
        const auto r = b.build();
        check(!r.ok, "unresolved referans -> ok=false");
        check(r.relationships.validate(r.objects).status == RelationshipStatus::Unresolved,
              "status Unresolved");
    }

    // --- Grafik: SizeFromParameter hedefi parametre olabilir (unresolved degil)
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("fin_plate", p).sizeFromParameter("fin_plate", "plateHeight");
        const auto r = b.build();
        check(r.ok, "SizeFromParameter parametre hedefi -> ok");
    }

    // --- Grafik: parallel+perpendicular model katmaninda celiski DEGILDIR ------
    // (Geometric iliskiler MODEL olarak kaydedilir; oryantasyon celiskisi cozucu
    // katmaninda — OrientationConstraint dilimi — uretilir. ConstraintSystem
    // su an yalniz mesafe kisitlarini cozer.)
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("a", p).plate("b", p).parallel("a", "b").perpendicular("a", "b");
        const auto r = b.build();
        check(r.ok, "parallel+perpendicular model katmani -> ok (kaydedilir)");
        check(r.relationships.count() == 2, "2 geometric iliski kaydedildi");
    }

    // --- Grafik: cycle tespiti MEVCUT ConstraintSystem'e delege -----------------
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("a", p).plate("b", p).attachedTo("a", "b").attachedTo("b", "a");
        const auto r = b.build();
        // Model katmani dongu COZMEZ (referans butunlugu gecerlidir).
        check(r.ok, "attachedTo dongusu model katmaninda ok (referans gecerli)");

        // Dongu tespiti ConstraintSystem adapter'i ile (mevcut cozucu):
        ConstraintSystem sys = r.relationships.toConstraintSystem(r.objects);
        check(!sys.solve(), "ConstraintSystem.solve() dongu -> false (Cyclic)");
        check(sys.position(HandleKey{"a"}).state == ConstraintState::Cyclic,
              "a handle Cyclic durumunda");
    }

    // --- Grafik: zincir dependency dongusuz -> ConstraintSystem cozer -----------
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("a", p).plate("b", p).plate("c", p)
            .attachedTo("b", "a").attachedTo("c", "b");  // c -> b -> a (dongu yok)
        const auto r = b.build();
        check(r.ok, "zincir dependency dongusuz -> ok");
        ConstraintSystem sys = r.relationships.toConstraintSystem(r.objects);
        check(sys.solve(), "ConstraintSystem.solve() dongusuz -> true");
    }

    // --- Grafik: generationPlanFromObjects koprusu ----------------------------
    {
        ComponentBuilder b;
        PlateSpec p;
        p.width = 100.0;
        p.height = 200.0;
        p.thickness = 8.0;
        BoltGroup bg;
        bg.countY = 2;
        WeldSpec w;
        w.throat = 5.0;
        b.plate("fin_plate", p).boltGroup("bolts", bg).weld("w", w);
        const auto r = b.build();
        check(r.ok, "plan koprusu builder ok");
        const GenerationPlan plan = generationPlanFromObjects(r.objects);
        check(plan.plates.size() == 1, "plan 1 plate");
        check(plan.boltGroups.size() == 1, "plan 1 boltGroup");
        check(plan.welds.size() == 1, "plan 1 weld");
        check(plan.cuts.empty(), "plan 0 cut");
        check(plan.plates[0].width == 100.0, "plan plate width tasindi");
    }

    // --- Grafik: sahiplik kenarlari cozum bagimliligi DEGIL (yapay Cyclic yok) --
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("a", p).plate("b", p).cuts("a", "b").ownedBy("a", "component");
        const auto r = b.build();
        check(r.ok, "cuts+ownedBy model katmani ok");
        check(r.relationships.directedEdges().size() == 2,
              "directedEdges sahiplik kenarlarini raporlar (2)");
        ConstraintSystem sys = r.relationships.toConstraintSystem(r.objects);
        check(sys.solve(), "sahiplik kenarlari solve() bagimliligi uretmez (true)");
    }

    // --- Grafik: OwnedBy hedefi kullanici parcasi olamaz ----------------------
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("a", p).ownedBy("a", "main");
        const auto r = b.build();
        check(!r.ok, "ownedBy(main) -> ok=false");
        check(r.relationships.validate(r.objects).status == RelationshipStatus::Conflict,
              "ownedBy(main) status Conflict");
    }
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("a", p).ownedBy("a", "secondary_0");
        const auto r = b.build();
        check(!r.ok, "ownedBy(secondary_0) -> ok=false");
    }
    // ownedBy -> component (component instance'inin kendisi) gecerli.
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("a", p).ownedBy("a", "component");
        const auto r = b.build();
        check(r.ok, "ownedBy(component) -> ok");
    }

    // === C1: ComponentChildTable — tip-guvenli, deterministik child yonetimi ===
    {
        ComponentChildRecord plate;
        plate.roleKey = "fin_plate";
        plate.kind = ComponentObjectKind::Plate;
        plate.plate = PlateSpec{};

        ComponentChildTable table;
        check(table.insert(plate), "table.insert yeni plate -> ok");
        check(table.size() == 1, "tablo boyutu 1");

        // Tekrarlanan roleKey deterministik reddedilir.
        check(!table.insert(plate), "tekrarlanan roleKey -> reddedildi");

        // Ayni roleKey FARKLI turde -> tip-guvenlik reddi.
        ComponentChildRecord bolt;
        bolt.roleKey = "fin_plate";  // ayni roleKey
        bolt.kind = ComponentObjectKind::BoltGroup;
        bolt.boltGroup = BoltGroup{};
        check(!table.insert(bolt), "ayni roleKey farkli tur -> reddedildi");

        // Gecersiz kayit (kind-payload uyusmazligi) -> reddedildi.
        ComponentChildRecord bad;
        bad.roleKey = "bad_child";
        bad.kind = ComponentObjectKind::Plate;  // plate payload yok
        check(!table.insert(bad), "kind-payload uyusmazligi -> reddedildi");

        // Construction kind -> kalici child degil.
        ComponentChildRecord ctor;
        ctor.roleKey = "helper_pt";
        ctor.kind = ComponentObjectKind::ConstructionPoint;
        check(!table.insert(ctor), "construction child -> reddedildi");
        check(!isValidComponentChildRecord(ctor), "isValidComponentChildRecord construction false");

        // assignEntityId + entityIdFor.
        const EntityId eid = static_cast<EntityId>(42);
        check(table.assignEntityId("fin_plate", eid), "assignEntityId -> ok");
        check(table.entityIdFor("fin_plate") == eid, "entityIdFor eslestirildi");
        check(!table.assignEntityId("yok", eid), "assignEntityId bilinmeyen -> false");

        // Deterministik sira: lexicographic roleKey.
        ComponentChildRecord aa;
        aa.roleKey = "a_first";
        aa.kind = ComponentObjectKind::BoltGroup;
        aa.boltGroup = BoltGroup{};
        check(table.insert(aa), "ikinci child ekle");
        const auto kids = table.children();
        check(kids.size() == 2, "children() 2 kayit");
        check(kids[0].roleKey == "a_first", "deterministik: a_first once");
        check(kids[1].roleKey == "fin_plate", "deterministik: fin_plate sonra");

        // remove deterministik.
        check(table.remove("a_first"), "remove a_first -> true");
        check(!table.remove("a_first"), "remove tekrar -> false");
        check(table.size() == 1, "remove sonrasi 1 kayit");
    }

    // === C1: componentPlanFromObjects — roleKey korunur ===
    {
        ComponentBuilder b;
        PlateSpec p;
        p.width = 100.0;
        p.height = 200.0;
        p.thickness = 8.0;
        BoltGroup bg;
        bg.countY = 2;
        b.plate("fin_plate", p).boltGroup("beam_bolts", bg)
            .attachedTo("beam_bolts", "fin_plate");
        const auto r = b.build();
        check(r.ok, "C1 plan builder ok");
        const ComponentPlan plan = componentPlanFromObjects(r.objects, r.relationships);
        check(plan.ok, "C1 plan ok");
        check(plan.plan.plates.size() == 1, "plan 1 plate (legacy)");
        check(plan.plan.boltGroups.size() == 1, "plan 1 boltGroup (legacy)");
        check(plan.children.size() == 2, "plan 2 child (roleKey korunur)");
        // Deterministik sira: beam_bolts < fin_plate (lexicographic).
        check(plan.children[0].roleKey == "beam_bolts", "child[0] beam_bolts");
        check(plan.children[1].roleKey == "fin_plate", "child[1] fin_plate");
        check(plan.children[1].kind == ComponentObjectKind::Plate, "child[1] kind Plate");
        check(plan.children[1].plate.has_value() && plan.children[1].plate->width == 100.0,
              "child[1] uretim parametreleri korunur");
        // dependencies: beam_bolts -> fin_plate (AttachedTo from=beam_bolts to=fin_plate).
        check(plan.children[0].dependencies.size() == 1, "beam_bolts 1 bagimlilik");
        check(plan.children[0].dependencies[0] == "fin_plate", "beam_bolts -> fin_plate");
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
