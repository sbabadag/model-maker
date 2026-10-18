// Snap derinlik/filtre cekirdek testi — OCC GEREKTIRMEZ, platformdan bagimsiz.
//
// IKI KULLANICI SIKAYETI (plan gorunusu):
//  A) "mid point snap bazen calismiyor" — Solid stilde bir cizginin/kenarin orta
//     noktasina snap kayboluyordu (varsayilan stil Solid!).
//  B) "plan gorunusunde midpoint yanlis yere snap'liyor" — plan gorunusunde
//     GORUNUR ust kenar ile ALTINDAKI gizli kenar AYNI ekran noktasina iz
//     dusurulur; esit ekran mesafesinde kenar listesi siralamasina gore GIZLI
//     kenar kazaniyordu (olcum: IPE200 ust kenar z=0 yerine z=-8.5 / -200).
//
// Kok nedenler (gercek kodla olculdu):
//  1) choose(): esit ekran mesafesinde yalnizca ONCELIK bakiyordu; derinlik
//     yok -> siralama bagimli. FIX: esit oncelikte IZLEYICIYE YAKIN kazanir.
//  2) solid gorunurluk filtresi: yuzu OLMAYAN modelde (cizgi/polyline) ve
//     normal yonu ic/dis olan OCC katilarinda kenarlari eliyordu.
//     FIX: (a) siniflandirma yoksa filtre uygulanmaz, (b) normal model
//     merkezine gore DISA yonlendirilir.
//
// Bu dosya GERCEK SnapEngine::snap3D()'yi cagirir (kopya mantik yok).
#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

// Uzun kutu: X boyunca L, Y genislik w (ortalanmis), ust yuz z=0, govde asagi h.
// KENAR SIRASI ONEMLI: gizli (alt) kenarlar ONCE listelenir ki derinlik
// tercihi olmadan tie GIZLI kenari secsin (regresyonu ortaya cikarir).
static WireframeModel beamNoFaces(double L, double w, double h) {
    const double y0 = -w / 2.0, y1 = w / 2.0, zt = 0.0, zb = -h;
    return WireframeModel{
        {{0, y0, zt}, {L, y0, zt}, {L, y1, zt}, {0, y1, zt},
         {0, y0, zb}, {L, y0, zb}, {L, y1, zb}, {0, y1, zb}},
        {{4, 5}, {0, 1}, {5, 6}, {1, 2}, {6, 7}, {2, 3},
         {7, 4}, {3, 0}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
}

// Yuzlu kutu. winding=+1 disa (dogru), winding=-1 ice (OCC tesselasyonunda
// gorulen durum) — normalin ISARETI ters gelir.
static WireframeModel boxWithFaces(double L, double w, double h, int winding) {
    const double y0 = -w / 2.0, y1 = w / 2.0, zt = 0.0, zb = -h;
    std::vector<Vec3> v{{0, y0, zt}, {L, y0, zt}, {L, y1, zt}, {0, y1, zt},
                        {0, y0, zb}, {L, y0, zb}, {L, y1, zb}, {0, y1, zb}};
    // Disa bakan dortgenler (CCW disaridan bakinca).
    const int quads[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
                             {1, 5, 6, 2}, {0, 3, 7, 4}, {1, 2, 6, 5}};
    std::vector<Face> faces;
    for (const auto& q : quads) {
        const std::size_t a = q[0], b = q[1], c = q[2], d = q[3];
        if (winding > 0) { faces.push_back({a, b, c}); faces.push_back({a, c, d}); }
        else             { faces.push_back({c, b, a}); faces.push_back({d, c, a}); }
    }
    // Kenarlar: ust ve alt dortgen + duseyler.
    return WireframeModel{std::move(v),
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4},
         {0, 4}, {1, 5}, {2, 6}, {3, 7}}, std::move(faces)};
}

int main() {
    const int W = 1200, H = 800;
    const double L = 3000, w = 300, h = 400;
    const Vec3 topMid{L / 2, -w / 2, 0.0};   // GORUNUR ust kenar ortasi

    // PLAN/UST gorunus: ekran saga +X, yukari +Y, bakis asagi (-Z yonu). Bu
    // zaman izleyici +Z tarafinda -> view-z BUYUK = YAKIN.
    Camera topCam;
    topCam.setViewBasis(Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{L / 2, 0.0, 0.0});
    const Vec2 s = topCam.project(topMid, W, H);

    // 1) DERINLIK TERCIHI: yuzsuz kiriş, Wireframe (filtre YOK) — saf tie.
    {
        Document doc; doc.addModel(beamNoFaces(L, w, h));
        const SnapResult r = SnapEngine::snap3D(s, doc, topCam, W, H, 10.0, WorkPlane::world(),
                                                true, false, std::nullopt, nullptr, false);
        check(r.type == SnapType::Midpoint, "1a yuzsuz/Wireframe: orta nokta bulunur");
        check(approx(r.point.z, 0.0), "1b yuzsuz/Wireframe: GORUNUR ust kenar secilir (z=0)");
        check(approx(r.point.y, -w / 2), "1c yuzsuz/Wireframe: y dogru");
    }

    // 2) FILTRE KORUMASI: yuzsuz kiriş, SOLID (visibleEdgesOnly=true).
    //    Eski kod yuzsuz modelde TUM kenarlari eliyordu -> None.
    {
        Document doc; doc.addModel(beamNoFaces(L, w, h));
        const SnapResult r = SnapEngine::snap3D(s, doc, topCam, W, H, 10.0, WorkPlane::world(),
                                                true, false, std::nullopt, nullptr, true);
        check(r.type == SnapType::Midpoint, "2a yuzsuz/Solid: orta nokta YOK OLMUYOR (varsayilan Solid)");
        check(approx(r.point.z, 0.0), "2b yuzsuz/Solid: yine gorunur ust kenar (z=0)");
    }

    // 3) Duz CIZGI (en yaygin durum), Solid: orta nokta snap'i calisir.
    {
        Document doc;
        doc.addModel(WireframeModel::line({0, 0, 0}, {4000, 0, 0}));
        const Vec2 ls = topCam.project({2000, 0, 0}, W, H);
        const SnapResult r = SnapEngine::snap3D(ls, doc, topCam, W, H, 10.0, WorkPlane::world(),
                                                true, false, std::nullopt, nullptr, true);
        check(r.type == SnapType::Midpoint, "3a cizgi/Solid: MIDPOINT snap'i calisir");
        check(approx(r.point.x, 2000.0), "3b cizgi/Solid: orta nokta x=2000");
    }

    // 4) TERS NORMAL (OCC tesselasyonu): yuzu olan kutu, normaller ICE donuk.
    //    Model merkezine gore disa yonlendirme olmadan ust yuz "gizli" sayilir.
    {
        Document doc; doc.addModel(boxWithFaces(L, w, h, -1));
        const SnapResult r = SnapEngine::snap3D(s, doc, topCam, W, H, 10.0, WorkPlane::world(),
                                                true, false, std::nullopt, nullptr, true);
        check(r.type == SnapType::Midpoint, "4a ters-normal/Solid: orta nokta bulunur");
        check(approx(r.point.z, 0.0), "4b ters-normal/Solid: normal disa yonlendirilir, ust kenar secilir");
    }

    // 5) DOGRU NORMAL (disa): ayni sonuc — isaret duzeltmesi bozmuyor.
    {
        Document doc; doc.addModel(boxWithFaces(L, w, h, +1));
        const SnapResult r = SnapEngine::snap3D(s, doc, topCam, W, H, 10.0, WorkPlane::world(),
                                                true, false, std::nullopt, nullptr, true);
        check(approx(r.point.z, 0.0), "5a dogru-normal/Solid: ust kenar (z=0)");
    }

    // 6) ONCELIK KORUNUR: tam bir UCTE snap Endpoint doner (midpoint'e kaymaz).
    {
        Document doc; doc.addModel(beamNoFaces(L, w, h));
        const Vec2 es = topCam.project({0, -w / 2, 0}, W, H);
        const SnapResult r = SnapEngine::snap3D(es, doc, topCam, W, H, 10.0, WorkPlane::world(),
                                                true, false, std::nullopt, nullptr, true);
        check(r.type == SnapType::Endpoint, "6a ucte: Endpoint onceligi korunur");
    }

    // 7) YAN GORUNUS regresyonu: bakis -Y -> ekran (x,z). Is duzlemi GORUNUS
    //    DUZLEMI olmali (normali bakis yonu): yatay bir duzlem yatay bakisa
    //    PARALELdir, isin kesismez (unproject basarisiz -> erken None).
    {
        Document doc; doc.addModel(boxWithFaces(L, w, h, -1));
        Camera sideCam;
        sideCam.setViewBasis(Vec3{1, 0, 0}, Vec3{0, 0, 1}, Vec3{L / 2, 0.0, -h});
        const WorkPlane viewPlane =
            WorkPlane::fromViewDirection({L / 2.0, -w / 2, -h}, Vec3{0.0, -1.0, 0.0});
        const Vec2 bs = sideCam.project({L / 2, -w / 2, -h}, W, H);   // alt siluet ortasi
        const SnapResult r = SnapEngine::snap3D(bs, doc, sideCam, W, H, 10.0, viewPlane,
                                                true, false, std::nullopt, nullptr, true);
        check(r.type == SnapType::Midpoint, "7a yan gorunus: alt siluet ortasi bulunur");
        check(approx(r.point.z, -h), "7b yan gorunus: z=-h (alt siluet)");
        check(approx(r.point.y, -w / 2), "7c yan gorunus: izleyiciye YAKIN siluet kenari (y=-w/2)");
    }

    std::printf("\n%d kontrol, %d hata\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
