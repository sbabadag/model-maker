// KONSTRUKSIYON DUZLEM CERCEVESI (P0 GUI hazirlik) — cekirdek test.
//
// constructionPlaneFrame() ConstructionPlane modelinin yerel cercevesini
// (origin/u/v/normal) 4 koseden yeniden kurar. Bu cerceve P0'da duzlem
// EKSENLERINI ve P1'de Fitting kesme duzlemini besler; MMW8'de ayri bir
// alan tasimadigimiz icin kayit/ac sonrasi da koselerden aynen geri
// gelmelidir. Bu test o sozlesmeyi kilitler:
//   - addConstructionPlane -> frame: origin/u/v/normal birebir eslesir
//   - tasima (moveModels) sonrasi frame orijini otelenir, eksenler degismez
//   - degajerese: Line/Normal/yanlis kose sayisi -> nullopt
//   - size semantigi: kose kenari = size, centroid = duzlem origin
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

    // 1) 3 noktadan egimli duzlem -> addConstructionPlane -> cerceve.
    const Vec3 p0{100.0, 0.0, 0.0};
    const Vec3 p1{100.0, 1000.0, 300.0};
    const Vec3 p2{600.0, 0.0, 120.0};
    const auto plane = WorkPlane::fromThreePoints(p0, p1, p2);
    check(plane.has_value(), "fromThreePoints basarili");
    if (!plane) return 2;

    const double size = 400.0;
    const std::size_t idx = doc.addConstructionPlane(*plane, size);
    const auto frame = constructionPlaneFrame(doc.models()[idx]);
    check(frame.has_value(), "constructionPlaneFrame duzlem icin cerceve doner");
    if (!frame) return 2;

    check(near3(frame->origin, plane->origin), "origin == duzlem origin (centroid)");
    check(near3(frame->u, plane->u), "u ekseni korunur");
    check(near3(frame->v, plane->v), "v ekseni korunur");
    check(near3(frame->normal, plane->normal), "normal korunur");

    // 2) size semantigi: kose kenari = size.
    const auto& v = doc.models()[idx].vertices();
    const double edgeU = std::hypot(v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z);
    const double edgeV = std::hypot(v[3].x - v[0].x, v[3].y - v[0].y, v[3].z - v[0].z);
    check(std::abs(edgeU - size) < 1e-9 && std::abs(edgeV - size) < 1e-9,
          "kose kenari == size (u ve v yonunde)");

    // 3) Tasima: frame orijini otelenir, eksenler degismez.
    const Vec3 disp{50.0, -30.0, 70.0};
    doc.pushSnapshot();
    doc.moveModels({idx}, disp);
    const auto frame2 = constructionPlaneFrame(doc.models()[idx]);
    check(frame2.has_value(), "tasima sonrasi cerceve gecerli");
    if (frame2) {
        check(near3(frame2->origin, plane->origin + disp), "tasima sonrasi origin otelenir");
        check(near3(frame2->u, plane->u) && near3(frame2->v, plane->v) &&
                  near3(frame2->normal, plane->normal),
              "tasima sonrasi eksenler degismez");
    }
    doc.undo();
    doc.clearHistory();

    // 4) Degenerese: Line / normal cizgi / nokta -> nullopt.
    const std::size_t lineIdx = doc.addConstructionLine({0, 0, 0}, {1, 0, 0});
    check(!constructionPlaneFrame(doc.models()[lineIdx]).has_value(),
          "Line icin cerceve nullopt");
    doc.addLine({0, 0, 0}, {1, 1, 1});
    check(!constructionPlaneFrame(doc.models()[doc.models().size() - 1]).has_value(),
          "normal cizgi icin cerceve nullopt");

    // 5) MMW8 round-trip: cerceve kayit/ac sonrasi da korunur (koseler sakli).
    {
        Document loaded;
        // addConstructionPlane ile ayni geometriyi yeniden kur (round-trip
        // kodek testi mmw8_document_test'te; burada cerceve sozlesmesini
        // kilitle: ayni koselerden ayni cerceve).
        const std::size_t li = loaded.addConstructionPlane(*plane, size);
        const auto lf = constructionPlaneFrame(loaded.models()[li]);
        check(lf.has_value() && near3(lf->normal, plane->normal) &&
                  near3(lf->origin, plane->origin),
              "yeniden kurulan koselerden ayni cerceve");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
