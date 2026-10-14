// KESME NESNELERI (P3) — cekirdek test.
//
// CutModifier (PlaneCut/Fitting/PolygonCut/BooleanCut) kalici + duzenlenebilir
// modifier olarak Document'ta indeks-bazli tutulur; hedef/arac parcalar EntityId
// ile referanslanir (parca kimligi korunur). Duzlem/cokgen/kesisim matematigi
// OCC'siz ve Linux'ta dogrulanir; gercek B-Rep boolean OCC katmaninda (Windows).
//
// Kapsam: duzlem isaretli uzaklik/taraf, izdusum, cokgen normali/ic-dis testi,
// dogru-duzlem kesisimi, fitting trim mesafesi, kose siniflandirmasi, Document
// CRUD + undo/redo.

#include "model_maker/cut_modifier.hpp"
#include "model_maker/document.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static bool near3(const Vec3& a, const Vec3& b, double tol = 1e-9) {
    return std::abs(a.x - b.x) < tol && std::abs(a.y - b.y) < tol && std::abs(a.z - b.z) < tol;
}

int main() {
    const Vec3 o{0, 0, 0};
    const Vec3 nz{0, 0, 1}; // XY duzlemi

    // 1) Isaretli uzaklik + taraf.
    check(std::abs(signedDistanceToPlane(o, nz, {1, 2, 5}) - 5.0) < 1e-9, "isaretli uzaklik +Z");
    check(std::abs(signedDistanceToPlane(o, nz, {1, 2, -3}) + 3.0) < 1e-9, "isaretli uzaklik -Z");
    check(planeSide(o, nz, {0, 0, 1}) == 1, "taraf +");
    check(planeSide(o, nz, {0, 0, -1}) == -1, "taraf -");
    check(planeSide(o, nz, {0, 0, 0}) == 0, "taraf uzerinde");

    // 2) Izdusum.
    check(near3(projectToPlane(o, nz, {4, 7, 9}), {4, 7, 0}), "izdusum normal bileseni atar");

    // 3) Cokgen normali (Newell).
    {
        const auto n = polygonNormal({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}});
        check(n.has_value(), "cokgen normali var");
        if (n) check(std::abs(n->z - 1.0) < 1e-9, "XY cokgeni normali +Z");
        check(!polygonNormal({{0, 0, 0}, {1, 0, 0}}).has_value(), "dejenere cokgen (2 nokta) nullopt");
        check(!polygonNormal({{0, 0, 0}, {0, 0, 1}, {0, 0, 2}}).has_value(),
              "dogrusal cokgen nullopt");
    }

    // 4) Ic-dis testi (birim kare).
    {
        const std::vector<Vec3> square{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        check(pointInPolygon(square, std::nullopt, {0.5, 0.5, 0}), "ic nokta");
        check(pointInPolygon(square, std::nullopt, {2.0, 2.0, 0}) == false, "dis nokta");
        check(pointInPolygon(square, std::nullopt, {1.5, 0.5, 0}) == false, "karenin saginda dis nokta");
    }

    // 5) Dogru-duzlem kesisimi.
    {
        const auto hit = segmentPlaneIntersection({0, 0, 1}, {0, 0, -1}, o, nz);
        check(hit.has_value(), "kesisim var");
        if (hit) check(near3(*hit, {0, 0, 0}), "kesisim noktasi dogru");
        check(!segmentPlaneIntersection({0, 0, 1}, {1, 0, 1}, o, nz).has_value(),
              "paralel dogru kesisimsiz");
        check(!segmentPlaneIntersection({0, 0, 1}, {0, 0, 2}, o, nz).has_value(),
              "parca disinda kesisim yok");
    }

    // 6) Fitting trim mesafesi.
    {
        const auto d = fittingTrimDistance({0, 0, 5}, {0, 0, 0}, o, nz);
        check(d.has_value(), "trim mesafesi var");
        if (d) check(std::abs(*d - 5.0) < 1e-9, "trim = 5 mm");
    }

    // 7) Kose siniflandirmasi (PlaneCut on karari).
    {
        const auto sides = classifyVerticesAgainstPlane(
            {{0, 0, 1}, {1, 1, 1}, {0, 0, -1}, {2, 2, -2}}, o, nz);
        check(sides.size() == 4, "4 kose siniflandirildi");
        check(sides[0] == 1 && sides[1] == 1, "+ taraf koseleri");
        check(sides[2] == -1 && sides[3] == -1, "- taraf koseleri");
    }

    // 8) Document CRUD + undo/redo.
    {
        Document doc;
        const EntityId partA = doc.modelId(doc.addConstructionPoint({0, 0, 0}));
        const EntityId partB = doc.modelId(doc.addConstructionPoint({10, 0, 0}));

        CutModifier planeCut;
        planeCut.kind = CutKind::PlaneCut;
        planeCut.target = partA;
        planeCut.planeOrigin = {5, 0, 0};
        planeCut.planeNormal = {1, 0, 0};
        planeCut.name = L"Kesim 1";

        doc.pushSnapshot();
        const std::size_t ci = doc.addCutModifier(planeCut);
        check(doc.cutModifiers().size() == 1, "addCutModifier ekler");
        check(doc.cutModifiers()[ci].target == partA, "hedef parca EntityId korunur");
        check(doc.cutModifiers()[ci].kind == CutKind::PlaneCut, "tur korunur");

        // Duzenle (replace) + undo.
        CutModifier edited = planeCut;
        edited.keepPositive = false;
        doc.pushSnapshot();
        check(doc.setCutModifier(ci, edited), "setCutModifier duzenler");
        check(doc.cutModifiers()[ci].keepPositive == false, "duzenlenen deger uygulandi");
        check(doc.undo(), "undo duzenleme");
        check(doc.cutModifiers()[ci].keepPositive == true, "undo eski degeri geri getirdi");

        // Silme + undo.
        doc.pushSnapshot();
        doc.removeCutModifier(ci);
        check(doc.cutModifiers().empty(), "removeCutModifier siler");
        check(doc.undo(), "undo silme");
        check(doc.cutModifiers().size() == 1 && doc.cutModifiers()[0].name == L"Kesim 1",
              "undo modifier geri getirdi");

        // Redo.
        check(doc.redo(), "redo silme");
        check(doc.cutModifiers().empty(), "redo sonrasi bos");

        // BooleanCut turu: arac EntityId referansi.
        CutModifier booleanCut;
        booleanCut.kind = CutKind::BooleanCut;
        booleanCut.target = partA;
        booleanCut.tool = partB;
        doc.pushSnapshot();
        doc.addCutModifier(booleanCut);
        check(doc.cutModifiers().size() == 1 && doc.cutModifiers()[0].tool == partB,
              "BooleanCut arac EntityId referansi");
        doc.clearHistory();
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
