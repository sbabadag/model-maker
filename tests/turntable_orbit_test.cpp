// TURNTABLE ORBIT cekirdek testi — OCC GEREKTIRMEZ.
//
// Profesyonel 3B navigasyon sarti: MMB orbit bir "turntable" olmali —
// yatay fare = yaw (dunya Z/dusey ekseni), dikey fare = pitch (kamera X/
// yatay ekseni), dunya-yukari Z korunur, roll birikmez, pitch ±89° ile
// sinirli (devrilme/flip yok). Bu dosya GERCEK Camera::rotate()/rotateAbout()/
// clampPitchDelta() kodunu cagirir; model_maker_core'a baglanir.
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
static bool approx(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

// R_ row-major: satir0 = kamera sag, satir1 = kamera yukari, satir2 = bakis
// yonu (izleyiciye dogru). viewTransform(e_basis)[i] = R_[3*i + basis], yani
// bir "satir" i bilesen bilesen {viewTransform(e_x)[i], viewTransform(e_y)[i],
// viewTransform(e_z)[i]} olarak yeniden kurulur.
static Vec3 rightAxis(const Camera& c) {
    return {c.viewTransform(Vec3{1, 0, 0}).x, c.viewTransform(Vec3{0, 1, 0}).x,
            c.viewTransform(Vec3{0, 0, 1}).x};
}
static Vec3 upAxis(const Camera& c) {
    return {c.viewTransform(Vec3{1, 0, 0}).y, c.viewTransform(Vec3{0, 1, 0}).y,
            c.viewTransform(Vec3{0, 0, 1}).y};
}
static Vec3 viewDir(const Camera& c) {
    return {c.viewTransform(Vec3{1, 0, 0}).z, c.viewTransform(Vec3{0, 1, 0}).z,
            c.viewTransform(Vec3{0, 0, 1}).z};
}
static double elevation(const Camera& c) { return std::asin(viewDir(c).z); }

int main() {
    // --- 1) Yaw (yatay) dunya Z etrafinda: yukselis DEGISMEZ -------------
    {
        Camera c;
        c.reset();
        const double e0 = elevation(c);
        c.rotate(0.37, 0.0);
        c.rotate(-0.11, 0.0);
        check(approx(elevation(c), e0, 1e-12),
              "yaw (yatay): yukselis (elevation) degismez (Z ekseni etrafinda)");
    }

    // --- 2) Dunya-yukari Z korunur: kamera sag ekseni hep YATAY ----------
    {
        Camera c;
        c.reset();
        double worst = 0.0;
        for (int i = 0; i < 120; ++i) {
            c.rotate(0.041, -0.023);
            worst = std::max(worst, std::abs(rightAxis(c).z)); // sag eksenin Z bileseni
        }
        check(worst < 1e-9, "uzun orbit: kamera sag ekseni yatay kalir (roll birikmez)");
        check(upAxis(c).z > 0.0, "uzun orbit: ekran-yukari +Z yonu korunur (bas asagi degil)");
    }

    // --- 3) Pitch siniri: +89° ustu asla gecilmez (flip yok) --------------
    {
        Camera c;
        c.reset(); // ISO, yukselis ~35°
        // Yukariya dogru cok fazla pitch: sinir 89°'de durmali.
        for (int i = 0; i < 200; ++i) c.rotate(0.0, 0.05);
        check(elevation(c) <= Camera::kMaxOrbitPitch + 1e-9,
              "pitch siniri: yukselis <= +89° (yukari devrilme yok)");
        // Ekran-yukari hala yukari (flip olsaydi bas asagi donerdi).
        check(upAxis(c).z > 0.0, "pitch siniri: flip yok (ekran-yukari +Z)");
    }

    // --- 4) Pitch siniri: -89° alti asla gecilmez -------------------------
    {
        Camera c;
        c.reset();
        for (int i = 0; i < 400; ++i) c.rotate(0.0, -0.05);
        check(elevation(c) >= -Camera::kMaxOrbitPitch - 1e-9,
              "pitch siniri: yukselis >= -89° (asagi devrilme yok)");
        check(upAxis(c).z > 0.0, "pitch siniri (asagi): flip yok (ekran-yukari +Z)");
    }

    // --- 5) Pivot etrafinda donuste de sinir gecerli (rotateAbout) --------
    {
        Camera c;
        c.reset();
        c.setCenter3D(Vec3{0, 0, 0});
        const Vec3 pivot{120, -40, 30};
        for (int i = 0; i < 200; ++i) c.rotateAbout(pivot, 0.01, 0.05);
        check(elevation(c) <= Camera::kMaxOrbitPitch + 1e-9,
              "rotateAbout: pitch siniri da gecerli (89°)");
    }

    // --- 6) Yaw + pitch ayni anda (yatik capraz hareket) ------------------
    {
        Camera c;
        c.reset();
        const double e0 = elevation(c);
        // Yalniz yaw: yukselis sabit; sonra pitch: birebir artar.
        c.rotate(0.5, 0.2);
        check(approx(elevation(c), e0 + 0.2, 1e-9),
              "capraz hareket: yaw yukselisi bozmaz, pitch birebir eklenir");
    }

    std::printf("\n%d kontrol, %d hata\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
