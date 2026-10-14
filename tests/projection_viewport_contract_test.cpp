// PROJEKSIYON-VIEWPORT SOZLESMESI (BUG-UI-001 destegi).
//
// "Yarim pencere" kok nedeni: gomulu Win32 canvas'in GetClientRect'i, Qt
// host'un fiziksel olcusunden KUCUK (mantiksal) deger donunce, projeksiyon
// sahneyi yanlis (kucuk) bir viewport'a haritalar ve cizim tuvalin sol-ust
// kose/bolgesinde kalir. Bu test, Camera projeksiyonunun DOGRU sozlesmesini
// kilitler: sahne merkezi HER viewport boyutunda TAM ekran merkezine gider ve
// viewport boyutu dogrudan haritalamayi belirler — cagiranin gercek piksel
// boyutunu vermesi gerektigi (DPI uyusmazliginin gorunur belirtisi).
//
// OCC'siz, Linux'ta kosar. Gercek Camera::project / project2D / unproject2D /
// unprojectToPlane govdelerini cagirir.

#include "model_maker/camera.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static bool near2(const Vec2& a, const Vec2& b, double tol = 1e-9) {
    return std::abs(a.x - b.x) < tol && std::abs(a.y - b.y) < tol;
}

static bool near3(const Vec3& a, const Vec3& b, double tol = 1e-9) {
    return std::abs(a.x - b.x) < tol && std::abs(a.y - b.y) < tol &&
           std::abs(a.z - b.z) < tol;
}

int main() {
    Camera cam;
    cam.setView(StandardView::Isometric);
    cam.setCenter3D(Vec3{120.0, -40.0, 35.0});

    // 1) Sahne merkezi TAM viewport merkezine gider — HER boyut icin.
    //    (Kok neden baglantisi: viewport boyutu kucuk verilirse merkez
    //    sol-ust koseye kayar — "yarim pencere" belirtisi.)
    const Vec3 center = cam.center3D();
    for (const auto [w, h] : {std::pair{640, 480}, {1144, 777}, {1430, 971}, {3840, 2160}}) {
        const Vec2 p = cam.project(center, w, h);
        check(near2(p, {w * 0.5, h * 0.5}), "3B merkez -> viewport merkezi (boyut-bagimsiz)");
    }

    // 2) Viewport boyutlari FARKLI iken merkez haritasi farklidir (sozlesme:
    //    cagiranin verdigi boyut cizimi belirler). 1144 vs 1430 -> merkez
    //    143 px (0.5 * (1430-1144)) saga kayar; kucuk boyut vermek cizimi
    //    koseye iter.
    {
        const Vec2 small = cam.project(center, 1144, 777);
        const Vec2 large = cam.project(center, 1430, 971);
        check(!near2(small, large), "kucuk/large viewport merkezi ayrismali");
        check(near2({large.x - small.x, large.y - small.y},
                    {143.0, 97.0}, 1e-6),
              "merkez kaymasi = (deltaW/2, deltaH/2)");
    }

    // 3) 2B projeksiyon round-trip: project2D -> unproject2D birebir geri doner.
    cam.zoomBy(2.5);
    cam.pan2DByPixels(37.0, -18.0);
    {
        const Vec3 world{1234.5, -678.9, 0.0};
        const Vec2 s = cam.project2D(world, 1430, 971);
        const Vec3 back = cam.unproject2D(s, 1430, 971);
        check(near3(world, back, 1e-6), "2B project/unproject round-trip");
    }

    // 4) 3B projeksiyon -> is duzlemine (Z=0) round-trip.
    {
        const Vec3 world{50.0, 60.0, 0.0};
        const Vec2 s = cam.project(world, 1430, 971);
        const auto back = cam.unprojectToPlane(s, 1430, 971, 0.0);
        check(back.has_value(), "unprojectToPlane Z=0 sonuc uretir");
        if (back) check(near3(world, *back, 1e-6), "3B project -> Z=0 duzlemi round-trip");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
