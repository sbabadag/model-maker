// Ortho + OSNAP cekirdek testi — OCC GEREKTIRMEZ, platformdan bagimsizdir.
//
// Kullanici kurali: "ortho mode acikken snap tutmuyor". AutoCAD'de de obje
// snap'i (uc, orta, kesisim...) ORTHO'yu EZER; F8 yalnizca SERBEST imleci (ve
// grid snap'ini) en yakin eksene kilitler. Uygulama (updateHover ->
// Application::applyOrthoConstraint) bu yuzden applyOrtho/applyOrtho3D'yi
// preserveObjectSnaps=true ile cagirir.
//
// Bu dosya GERCEK applyOrtho()/applyOrtho3D() kodunu cagirir (kopya mantik
// yok); model_maker_core'a baglanir. Cagri sozlesmesi (uygulamanin hangi
// preserve degerini gectigi) ayrica tests/run_ortho_snap.py ile dogrulanir.
#include "model_maker/drafting.hpp"
#include "model_maker/camera.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
static bool nearV(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

int main() {
    const Vec3 anchor{1.0, -2.0, 0.5};
    const SnapResult endpoint{Vec3{7.0, 8.0, 9.0}, SnapType::Endpoint, 1.0};
    const SnapResult freeCursor{Vec3{6.0, 2.0, 0.5}, SnapType::None, 0.0};
    const SnapResult gridPoint{Vec3{6.0, 2.0, 0.5}, SnapType::Grid, 0.0};

    // 1) 2B: obje snap'i ortho'yu EZER (korunur).
    {
        const auto out = applyOrtho(anchor, endpoint, /*preserveObjectSnaps=*/true);
        check(out.type == SnapType::Endpoint, "2D: obje snap tipi korunur (Endpoint)");
        check(nearV(out.point, endpoint.point), "2D: obje snap noktasi degismez");
    }

    // 2) 2B: serbest imlec yine eksene kilitlenir (ortho calisiyor).
    {
        const auto out = applyOrtho(anchor, freeCursor, true);
        check(nearV(out.point, Vec3{6.0, anchor.y, 0.5}), "2D: serbest imlec X eksenine kilitlenir");
        check(out.orthoAxis == OrthoAxis::X, "2D: ortho ekseni X olarak raporlanir");
    }

    // 3) 2B: grid snap'i obje snap'i SAYILMAZ — ortho onu yine kisitlar.
    {
        const auto out = applyOrtho(anchor, gridPoint, true);
        check(nearV(out.point, Vec3{6.0, anchor.y, 0.5}), "2D: grid snap ortho ile kisitlanir");
    }

    // 4) preserve=true'nun gercekten ise yaradigi: preserve=false ise obje snap'i
    //    EZILIR (eski/bozuk davranis). Bu, ayrimin gercek oldugunu kanitlar.
    {
        const auto broken = applyOrtho(anchor, endpoint, /*preserveObjectSnaps=*/false);
        check(broken.type == SnapType::None && !nearV(broken.point, endpoint.point),
              "2D preserve=false: obje snap'i EZILIR (regresyon davranisi)");
    }

    Camera camera;
    constexpr int width = 900;
    constexpr int height = 700;
    const Vec2 endpointScreen = camera.project(endpoint.point, width, height);

    // 5) 3B, dunya duzlemi (X/Y/Z eksen kumesi): obje snap'i korunur.
    {
        const auto out = applyOrtho3D(anchor, endpointScreen, endpoint, camera, width, height,
                                      /*preserveObjectSnaps=*/true);
        check(out.type == SnapType::Endpoint, "3D dunya: obje snap tipi korunur");
        check(nearV(out.point, endpoint.point), "3D dunya: obje snap noktasi degismez");
    }

    // 6) 3B, is duzlemi eksenleri (+ normal): obje snap'i korunur; serbest imlec
    //    o duzlemin eksenine kilitlenir.
    {
        const double diagonal = std::sqrt(0.5);
        const WorkPlane tilted{Vec3{2.0, -1.0, 3.0}, Vec3{diagonal, 0.0, diagonal},
                               Vec3{0.0, 1.0, 0.0}, Vec3{-diagonal, 0.0, diagonal}};
        const auto snapOnPlane = applyOrtho3D(anchor, endpointScreen, endpoint, camera, width, height,
                                              tilted, /*includePlaneNormal=*/true,
                                              /*preserveObjectSnaps=*/true);
        check(snapOnPlane.type == SnapType::Endpoint, "3D is duzlemi: obje snap tipi korunur");
        check(nearV(snapOnPlane.point, endpoint.point), "3D is duzlemi: obje snap noktasi degismez");

        const Vec3 anchorOnPlane = tilted.fromPlane({0.0, 0.0});
        const Vec3 alongU = tilted.fromPlane({2.0, 0.0});
        const auto freeScreen = camera.project(alongU, width, height);
        const auto constrained = applyOrtho3D(anchorOnPlane, freeScreen, freeCursor, camera,
                                              width, height, tilted, true, true);
        check(constrained.point != freeCursor.point,
              "3D is duzlemi: serbest imlec yine eksene kilitlenir");
        const Vec3 delta = constrained.point - anchorOnPlane;
        const double acrossV = delta.x * tilted.v.x + delta.y * tilted.v.y + delta.z * tilted.v.z;
        check(near(acrossV, 0.0, 1e-6), "3D is duzlemi: kilit V bilesenini sifirlar (U ekseni)");
    }

    // 7) 3B preserve=false: obje snap'i ezilir (regresyon davranisinin kaniti).
    {
        const auto broken = applyOrtho3D(anchor, endpointScreen, endpoint, camera, width, height,
                                         /*preserveObjectSnaps=*/false);
        check(broken.type == SnapType::None && !nearV(broken.point, endpoint.point),
              "3D preserve=false: obje snap'i EZILIR (regresyon davranisi)");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
