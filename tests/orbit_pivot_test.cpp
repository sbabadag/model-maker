// Ekran donus merkezi (R tusu) cekirdek testi — OCC GEREKTIRMEZ.
//
// Kullanici kurali: "R tusu bir rotate merkezi belirler ... ctrl+mouse top bas
// ile ekrani o merkez etrafinda rotate eder". Yani secilen pivot etrafinda
// yorunge donusu yapilirken pivot EKRANDA SABIT kalmali (goruntu ziplamamali)
// ve rotasyonun ACI semantigi degismemeli (pivot yalnizca bakis merkezini
// degistirir).
//
// Bu dosya GERCEK Camera::rotateAbout()/rotate() kodunu cagirir (kopya mantik
// yok); model_maker_core'a baglanir. Uygulama baglantisi (R tusu, Ctrl+sol tus
// jesti) ayrica tests/run_rotate_center.py ile dogrulanir.
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

static constexpr int kWidth = 1280;
static constexpr int kHeight = 720;
// Piksel sabitligi esigi: yuvarlama hatasi kaynakli artik sapma ~1e-9..1e-6 px
// (gorunmez). Olculen degerler yazdirilir; esik onlarin ustunde ama hala
// "piksel piksel sabit" diyebilecek kadar siki.
static constexpr double kPixelTolerance = 1e-6;
static constexpr double kPixelToleranceLong = 1e-5;

// Pivotun ekran konumu (piksel).
static Vec2 screenOf(const Camera& camera, const Vec3& point) {
    return camera.project(point, kWidth, kHeight);
}
static double pixelDistance(const Vec2& a, const Vec2& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

int main() {
    // --- 1) Pivot EKRANDA SABIT: ana sart -------------------------------
    {
        Camera camera;
        camera.reset();
        camera.setCenter3D(Vec3{0.0, 0.0, 0.0});
        const Vec3 pivot{420.0, -180.0, 95.0}; // merkezden UZAK bir nokta
        const Vec2 before = screenOf(camera, pivot);

        double worst = 0.0;
        for (int step = 0; step < 60; ++step) {
            camera.rotateAbout(pivot, 0.037, -0.021);
            worst = std::max(worst, pixelDistance(before, screenOf(camera, pivot)));
        }
        check(worst < kPixelTolerance, "rotateAbout: pivot 60 adimda EKRANDA sabit (<=1e-6 px)");
        std::printf("     (en buyuk sapma = %.3e px)\n", worst);
    }

    // --- 2) Gorunum-uzayi farki da degismez (piksel sabitliginin kaynagi) --
    {
        Camera camera;
        camera.reset();
        camera.setCenter3D(Vec3{100.0, 50.0, -20.0});
        const Vec3 pivot{460.0, -230.0, 130.0};
        const Vec3 before = camera.viewTransform(pivot - camera.center3D());
        camera.rotateAbout(pivot, 0.5, 0.3);
        const Vec3 after = camera.viewTransform(pivot - camera.center3D());
        check(near(before.x, after.x, 1e-9) && near(before.y, after.y, 1e-9),
              "rotateAbout: R*(pivot-merkez) korunur (x,y)");
    }

    // --- 3) ACI SEMANTIGI degismez: R_ ayni, yalniz bakis merkezi kayar ----
    {
        Camera withPivot;
        withPivot.reset();
        withPivot.setCenter3D(Vec3{10.0, -10.0, 5.0});
        Camera plain = withPivot;
        const Vec3 pivot{300.0, 300.0, 0.0};
        withPivot.rotateAbout(pivot, 0.21, -0.13);
        plain.rotate(0.21, -0.13);
        // Ayni gorunus yonu (yaw/pitch/roll) — pivot yonu degistirmez.
        check(near(withPivot.yaw(), plain.yaw(), 1e-12) &&
              near(withPivot.pitch(), plain.pitch(), 1e-12) &&
              near(withPivot.roll(), plain.roll(), 1e-12),
              "rotateAbout: yon (yaw/pitch/roll) rotate() ile AYNI");
        check(!(near(withPivot.center3D().x, plain.center3D().x, 1e-9) &&
                near(withPivot.center3D().y, plain.center3D().y, 1e-9)),
              "rotateAbout: bakis merkezi pivot etrafinda kayar (farkli)");
    }

    // --- 4) Pivot = merkez ise davranis birebir eski rotate() -------------
    {
        Camera a;
        a.reset();
        a.setCenter3D(Vec3{7.0, -3.0, 11.0});
        Camera b = a;
        const Vec3 center = a.center3D();
        a.rotateAbout(center, 0.31, 0.17);
        b.rotate(0.31, 0.17);
        check(near(a.center3D().x, b.center3D().x, 1e-12) &&
              near(a.center3D().y, b.center3D().y, 1e-12) &&
              near(a.center3D().z, b.center3D().z, 1e-12),
              "pivot == merkez: bakis merkezi DEGISMEZ (eski davranis)");
        check(near(a.yaw(), b.yaw(), 1e-12) && near(a.pitch(), b.pitch(), 1e-12),
              "pivot == merkez: yon eski davranisla ayni");
    }

    // --- 5) Kayitli pivot: rotate() artik pivot etrafinda doner ----------
    {
        Camera camera;
        camera.reset();
        camera.setCenter3D(Vec3{0.0, 0.0, 0.0});
        const Vec3 pivot{-260.0, 140.0, 60.0};
        camera.setOrbitPivot(pivot);
        check(camera.hasOrbitPivot(), "setOrbitPivot: kayitli");
        const Vec2 before = screenOf(camera, pivot);
        double worst = 0.0;
        for (int step = 0; step < 40; ++step) {
            camera.rotate(0.05, 0.03); // uygulama yolu: rotate()
            worst = std::max(worst, pixelDistance(before, screenOf(camera, pivot)));
        }
        check(worst < kPixelTolerance, "kayitli pivot: rotate() pivot etrafinda doner (ekranda sabit)");
        check(near(camera.orbitPivot()->x, pivot.x, 1e-12),
              "kayitli pivot: nokta degismez (dunya noktasi)");

        // Pivot merkeze olan uzakligi korunur (yorunge yaricapi) — 3B uzaklik.
        const auto radiusOf = [&]() {
            return std::sqrt((camera.center3D().x - pivot.x) * (camera.center3D().x - pivot.x) +
                             (camera.center3D().y - pivot.y) * (camera.center3D().y - pivot.y) +
                             (camera.center3D().z - pivot.z) * (camera.center3D().z - pivot.z));
        };
        const double radius = radiusOf();
        camera.rotate(0.4, 0.2);
        const double radius2 = radiusOf();
        camera.rotate(-0.2, 0.1);
        const double radius3 = radiusOf();
        check(radius > 1.0, "kayitli pivot: yorunge yaricapi sifirdan buyuk (gercek yorunge)");
        check(near(radius, radius2, 1e-9) && near(radius, radius3, 1e-9),
              "kayitli pivot: yorunge yaricapi (3B uzaklik) sabit");
    }

    // --- 6) Uzun donus dizisi: birikimli sapma yok ------------------------
    {
        Camera camera;
        camera.reset();
        camera.setCenter3D(Vec3{-50.0, 25.0, 10.0});
        const Vec3 pivot{780.0, -410.0, 260.0};
        const Vec2 before = screenOf(camera, pivot);
        for (int step = 0; step < 500; ++step) camera.rotateAbout(pivot, 0.011, -0.007);
        const double drift = pixelDistance(before, screenOf(camera, pivot));
        check(drift < kPixelToleranceLong, "500 adim: birikimli piksel sapmasi yok (<=1e-5 px)");
        std::printf("     (500 adim sonrasi sapma = %.3e px)\n", drift);
    }

    // --- 7) Pan/zoom pivotu KORUR; sonraki donus yine sabit --------------
    {
        Camera camera;
        camera.reset();
        camera.setCenter3D(Vec3{0.0, 0.0, 0.0});
        const Vec3 pivot{200.0, 0.0, 0.0};
        camera.setOrbitPivot(pivot);
        camera.pan3DByPixels(35.0, -18.0);
        camera.zoom3DAt(Vec2{600.0, 300.0}, 1.35, kWidth, kHeight);
        check(camera.hasOrbitPivot(), "pan/zoom: pivot korunur (dunya noktasi)");
        const Vec2 now = screenOf(camera, pivot);
        camera.rotate(0.09, -0.05);
        check(pixelDistance(now, screenOf(camera, pivot)) < kPixelTolerance,
              "pan/zoom sonrasi donus: pivot yine ekranda sabit");
    }

    // --- 8) Gorunus degisiklikleri pivotu TEMIZLER -----------------------
    {
        Camera camera;
        camera.reset();
        camera.setOrbitPivot(Vec3{1.0, 2.0, 3.0});
        camera.reset();
        check(!camera.hasOrbitPivot(), "reset() pivotu temizler");

        camera.setOrbitPivot(Vec3{1.0, 2.0, 3.0});
        camera.setView(StandardView::Top);
        check(!camera.hasOrbitPivot(), "setView() pivotu temizler");

        camera.setOrbitPivot(Vec3{1.0, 2.0, 3.0});
        camera.setViewBasis(Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 0.0, 1.0}, Vec3{0.0, 0.0, 0.0});
        check(!camera.hasOrbitPivot(), "setViewBasis() (2 noktali gorunus) pivotu temizler");

        // Gecersiz taban (paralel vektor) no-op; pivot da durmali.
        camera.setOrbitPivot(Vec3{5.0, 5.0, 5.0});
        camera.setViewBasis(Vec3{1.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 0.0, 0.0});
        check(camera.hasOrbitPivot(), "gecersiz setViewBasis: no-op, pivot korunur");
    }

    // --- 9) Temizlendikten sonra rotate() eski (merkez) davranisa doner ----
    {
        Camera camera;
        camera.reset();
        camera.setCenter3D(Vec3{3.0, -4.0, 5.0});
        camera.setOrbitPivot(Vec3{900.0, 900.0, 0.0});
        camera.clearOrbitPivot();
        Camera plain = camera;
        camera.rotate(0.17, 0.09);
        plain.rotate(0.17, 0.09);
        check(near(camera.center3D().x, plain.center3D().x, 1e-12) &&
              near(camera.center3D().y, plain.center3D().y, 1e-12) &&
              near(camera.center3D().z, plain.center3D().z, 1e-12),
              "clearOrbitPivot sonrasi: merkez etrafinda eski davranis");
    }

    // --- 10) Her pencere kendi pivottunu tutar (kamera basina) ------------
    {
        Camera main;
        main.reset();
        main.setOrbitPivot(Vec3{10.0, 0.0, 0.0});
        Camera secondary;
        secondary.reset();
        check(main.hasOrbitPivot() && !secondary.hasOrbitPivot(),
              "pivot KAMERA basina: ikincil pencerede sizmaz");
        secondary.rotate(0.2, 0.1);
        check(main.hasOrbitPivot() && !secondary.hasOrbitPivot(),
              "ikincil pencerede donus ana pencerenin pivottunu etkilemez");
    }

    // --- 11) Sifir aci: gorunum hic degismez (no-op guvenligi) -----------
    {
        Camera camera;
        camera.reset();
        const Vec3 pivot{120.0, 60.0, 30.0};
        const Vec3 center = camera.center3D();
        camera.rotateAbout(pivot, 0.0, 0.0);
        check(near(camera.center3D().x, center.x, 1e-12) &&
              near(camera.center3D().y, center.y, 1e-12) &&
              near(camera.center3D().z, center.z, 1e-12),
              "sifir aci: bakis merkezi degismez");
    }

    // --- 12) Egik/iso gorunuste de piksel sabitligi ----------------------
    {
        Camera camera;
        camera.setView(StandardView::Isometric);
        camera.setCenter3D(Vec3{0.0, 0.0, 0.0});
        const Vec3 pivot{Vec3{-330.0, 275.0, -140.0}};
        const Vec2 before = screenOf(camera, pivot);
        double worst = 0.0;
        for (int step = 0; step < 90; ++step) {
            camera.rotateAbout(pivot, -0.045, 0.028);
            worst = std::max(worst, pixelDistance(before, screenOf(camera, pivot)));
        }
        check(worst < kPixelTolerance, "izometrik gorunus: pivot ekranda sabit");
    }

    std::printf("\n%d kontrol, %d hata\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
