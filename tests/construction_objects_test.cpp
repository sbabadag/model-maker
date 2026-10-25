// KONSTRUKSIYON NESNELERI (P2) — cekirdek test.
//
// Construction Point/Line/Plane standart WireframeModel olarak saklanir ve
// ConstructionKind etiketi tasir; boylece snap/secim/gumball/EntityId/undo
// zaten calisan model yollarinda GECERLIDIR. Bu test o sozlesmeyi kilitler:
//   - Point/Line/Plane etiketleri ve geometri (vertex/edge/face)
//   - constructionObjects()/isConstruction() ayristirma
//   - EntityId kararliligi (ekle/sil/undo boyunca)
//   - gorunurluk bayragi
//   - silme + geri alma ConstructionKind etiketini KORUR
//
// OCC'siz, Linux'ta kosar.

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
    Document doc;

    // 1) Point / Line / Plane ekleme + etiket + geometri.
    const std::size_t ip = doc.addConstructionPoint({10.0, 20.0, 30.0});
    const std::size_t il = doc.addConstructionLine({0, 0, 0}, {5, 0, 0});
    const std::size_t ipl = doc.addConstructionPlane(WorkPlane::world(), 100.0);
    check(doc.models().size() == 3, "3 nesne eklendi");

    check(doc.models()[ip].constructionKind() == ConstructionKind::Point, "Point etiketi");
    check(doc.models()[ip].isPointEntity(), "Point geometrisi (isPointEntity)");
    check(doc.models()[ip].vertices().size() == 1 &&
              near3(doc.models()[ip].vertices()[0], {10, 20, 30}),
          "Point konumu dogru");

    check(doc.models()[il].constructionKind() == ConstructionKind::Line, "Line etiketi");
    check(doc.models()[il].vertices().size() == 2 && doc.models()[il].edges().size() == 1,
          "Line geometrisi (2 vertex, 1 edge)");

    check(doc.models()[ipl].constructionKind() == ConstructionKind::Plane, "Plane etiketi");
    check(doc.models()[ipl].vertices().size() == 4 && doc.models()[ipl].faces().size() == 1,
          "Plane geometrisi (4 vertex, 1 face)");

    // 2) constructionObjects / isConstruction ayristirmasi.
    doc.addLine({0, 0, 0}, {1, 1, 1}); // normal cizgi (None)
    const auto cons = doc.constructionObjects();
    check(cons.size() == 3, "constructionObjects 3 dondurur");
    bool inOrder = cons.size() == 3 && cons[0] == ip && cons[1] == il && cons[2] == ipl;
    check(inOrder, "constructionObjects belge sirasinda");
    check(doc.isConstruction(ip) && doc.isConstruction(il) && doc.isConstruction(ipl),
          "isConstruction true");
    check(!doc.isConstruction(3), "normal model isConstruction false");
    check(doc.models()[3].constructionKind() == ConstructionKind::None, "normal model None");

    // 3) EntityId kararliligi: kimlik gecerli ve indeksle eslesir.
    check(doc.modelId(ip) != kInvalidEntityId, "Point gecerli EntityId alir");
    check(doc.indexOfEntity(doc.modelId(ip)) == ip, "indexOfEntity modelId ile eslesir");

    // 4) Gorunurluk bayragi.
    {
        const std::size_t before = doc.constructionObjects().size();
        const bool changed = doc.setConstructionVisible(false);
        check(changed, "setConstructionVisible(false) degisiklik uretir");
        for (const auto i : doc.constructionObjects())
            check(!doc.models()[i].properties().visible, "gorunmez (false) uygulandi");
        check(before == 3, "kontrol: 3 nesne vardi");
        doc.setConstructionVisible(true);
        for (const auto i : doc.constructionObjects())
            check(doc.models()[i].properties().visible, "gorunur (true) geri dondu");
    }

    // 5) Silme + geri alma ConstructionKind etiketini korur.
    {
        const EntityId pid = doc.modelId(ip);
        doc.pushSnapshot();
        doc.deleteModels({ip});
        check(doc.models().size() == 3, "silme sonrasi boyut 3");
        const bool undone = doc.undo();
        check(undone, "undo basarili");
        check(doc.models().size() == 4, "undo sonrasi boyut 4");
        const auto idx = doc.indexOfEntity(pid);
        check(idx.has_value(), "silinen nesne ayni kimlikle geri geldi");
        if (idx) {
            check(doc.models()[*idx].constructionKind() == ConstructionKind::Point,
                  "undo sonrasi Point etiketi korunur");
            check(near3(doc.models()[*idx].vertices()[0], {10, 20, 30}),
                  "undo sonrasi konum korunur");
        }
        doc.clearHistory();
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
