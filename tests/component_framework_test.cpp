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
    check(isGeometricRelationship(RelationshipKind::SizeFromMember), "SizeFromMember geometric");
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
            .ownedBy("fin_plate", "main");

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

    // --- Grafik: conflict (Parallel + Perpendicular) -------------------------
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("a", p).plate("b", p).parallel("a", "b").perpendicular("a", "b");
        const auto r = b.build();
        check(!r.ok, "parallel+perpendicular -> ok=false");
        check(r.relationships.validate(r.objects).status == RelationshipStatus::Conflict,
              "status Conflict");
    }

    // --- Grafik: cycle tespiti (attachedTo dongusu) --------------------------
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("a", p).plate("b", p).attachedTo("a", "b").attachedTo("b", "a");
        const auto r = b.build();
        check(!r.ok, "attachedTo dongusu -> ok=false");
        check(r.relationships.validate(r.objects).status == RelationshipStatus::Cyclic,
              "status Cyclic");
        const auto cyc = r.relationships.findCycles();
        check(cyc.size() >= 3, "findCycles dongu dondurur");
    }

    // --- Grafik: dongusuz dependency -> ok -----------------------------------
    {
        ComponentBuilder b;
        PlateSpec p;
        b.plate("a", p).plate("b", p).plate("c", p)
            .attachedTo("b", "a").attachedTo("c", "b");  // c -> b -> a (dongu yok)
        const auto r = b.build();
        check(r.ok, "zincir dependency dongusuz -> ok");
    }

    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
