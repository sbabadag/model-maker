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

    // 6b) Fitting OTOMATIK taraf secimi (Tekla "kisa taraf kesilir").
    {
        const Vec3 from{0, 0, 0};
        const Vec3 to{100, 0, 0};
        const Vec3 nx{1, 0, 0};

        // Duzlem x=20'de (from'a yakin): kisa taraf (0..20) atilir, uzun
        // (20..100, +X) korunur -> keepPositive = true.
        const auto k1 = fittingKeepPositive(from, to, {20, 0, 0}, nx);
        check(k1.has_value() && *k1 == true, "fitting: kisa from-tarafi atildi (koru +X)");

        // Duzlem x=80'de (to'ya yakin): uzun (0..80, -X) korunur -> false.
        const auto k2 = fittingKeepPositive(from, to, {80, 0, 0}, nx);
        check(k2.has_value() && *k2 == false, "fitting: kisa to-tarafi atildi (koru -X)");

        // Tam orta (x=50): esitlikte to-tarafi korunur -> true.
        const auto k3 = fittingKeepPositive(from, to, {50, 0, 0}, nx);
        check(k3.has_value() && *k3 == true, "fitting: orta nokta esitligi to-tarafini korur");

        // Normal TERS (-X), duzlem x=20: s_from=+20, s_to=-80 -> uzun to-tarafi
        // (-X) korunur -> keepPositive = false.
        const auto k4 = fittingKeepPositive(from, to, {20, 0, 0}, {-1, 0, 0});
        check(k4.has_value() && *k4 == false, "fitting: ters normal ile taraf secimi");

        // Duzlem ekseni KESMIYOR (x=150, iki uc da -X): uzatma -> nullopt.
        const auto k5 = fittingKeepPositive(from, to, {150, 0, 0}, nx);
        check(!k5.has_value(), "fitting: uzatma (kesmeyen duzlem) nullopt");

        // Eksen duzlem UZERINDE (XY duzlemi, normal +Z, eksen z=0): nullopt.
        const auto k6 = fittingKeepPositive(from, to, {0, 0, 0}, {0, 0, 1});
        check(!k6.has_value(), "fitting: eksen duzlem icinde nullopt");

        // Uc duzlemde (s_from=0, s_to=+80): to-tarafi korunur -> true.
        const auto k7 = fittingKeepPositive({20, 0, 0}, to, {20, 0, 0}, nx);
        check(k7.has_value() && *k7 == true, "fitting: uc duzlemde uzun taraf korunur");
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
