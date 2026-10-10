// Profil kesit cercevesi cekirdek testi (OCC GEREKTIRMEZ).
//
// Kullanici kurali: "Profil cizimlerinde her zaman ALT BASLIK XY duzlemine
// PARALEL olacak" — kesitin genislik ekseni yatay secilir; baslik plakalari
// (normal = height) yatay, govde dusey kalir.
//
// Bu kural TEK KAYNAKTAN (mm::profileSectionFrame) gelir; hem kati uretimi
// (extrudeProfileSolid) hem gumball yerel cercevesi ayni fonksiyonu cagirir.
#include "model_maker/geometry.hpp"

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
static double lengthOf(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
static double dotOf(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 crossOf(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// "Alt baslik XY duzlemine paralel" = baslik plakasinin normali (height) dunya
// Z'sine paralel ve genislik ekseni (width) yatay.
static bool flangeParallelToXY(const ProfileSectionFrame& f) {
    return near(std::abs(f.height.z), 1.0, 1e-9) && near(f.width.z, 0.0, 1e-12);
}

int main() {
    // 1) Yatay kiris +X: genislik Y, yukseklik Z → basliklar XY'ye PARALEL.
    {
        const auto f = profileSectionFrame(Vec3{1, 0, 0});
        check(nearV(f.axis, {1, 0, 0}), "axis +X korunur");
        check(nearV(f.width, {0, 1, 0}), "+X uye: genislik ekseni +Y (yatay)");
        check(nearV(f.height, {0, 0, 1}), "+X uye: yukseklik ekseni +Z (baslik XY'ye paralel)");
        check(flangeParallelToXY(f), "+X uye: ALT BASLIK XY DUZLEMINE PARALEL");
    }

    // 2) Yatay kiris +Y (eski OCC cercevesinin yatik getirdigi ikinci durum).
    {
        const auto f = profileSectionFrame(Vec3{0, 1, 0});
        check(nearV(f.width, {-1, 0, 0}), "+Y uye: genislik ekseni yatay (-X)");
        check(flangeParallelToXY(f), "+Y uye: ALT BASLIK XY DUZLEMINE PARALEL");
    }

    // 3) Plan icinde capraz uye (-45°): ayni kural, ayni sonuc.
    {
        const auto f = profileSectionFrame(Vec3{0.7071067811865476, 0.7071067811865476, 0});
        check(flangeParallelToXY(f), "plan caprazi uye: ALT BASLIK XY DUZLEMINE PARALEL");
        check(nearV(f.height, {0, 0, 1}), "plan caprazi uye: yukseklik tam +Z");
    }

    // 4) Egik uye (45° X-Z): govde DUSEY kalir (genislik yatay); baslik uyeye
    //    dik olur — celikte standart yonelim (egik kesit geometrik olarak
    //    XY'ye paralel olamaz, ama web duseydir).
    {
        const auto f = profileSectionFrame(Vec3{0.7071067811865476, 0, 0.7071067811865476});
        check(near(f.width.z, 0.0, 1e-12), "egik uye: genislik ekseni YATAY (web dusey)");
        check(nearV(f.width, {0, 1, 0}), "egik uye: genislik +Y");
    }

    // 5) Dusey uye (kolon): kesit dunya X/Y duzleminde kalir (onceki davranis).
    {
        const auto up = profileSectionFrame(Vec3{0, 0, 1});
        check(nearV(up.width, {1, 0, 0}) && nearV(up.height, {0, 1, 0}),
              "kolon (+Z): kesit dunya X/Y'de");
        const auto down = profileSectionFrame(Vec3{0, 0, -1});
        check(nearV(down.width, {1, 0, 0}) && nearV(down.height, {0, -1, 0}),
              "kolon (-Z): sag elli cerceve korunur");
    }

    // 6) Sifir uzunluk → dunya cercevesi (cokme yok).
    {
        const auto f = profileSectionFrame(Vec3{0, 0, 0});
        check(nearV(f.axis, {0, 0, 1}) && nearV(f.width, {1, 0, 0}) && nearV(f.height, {0, 1, 0}),
              "sifir eksen: dunya cercevesi, cokme yok");
    }

    // 7) Her yonde ortormal + sag elli + birim vektor.
    {
        const double dirs[][3] = {{1, 0, 0},   {0, 1, 0},   {0, 0, 1},   {0, 0, -1}, {-1, 0, 0},
                                  {1, 1, 0},   {1, -2, 0},  {3, 4, 0},   {0, 1, 1},  {1, 2, 3},
                                  {-2, 1, -3}, {0.5, 0, 2}, {2, 0, -1},  {0.1, 0.1, 5}};
        bool orthonormal = true, rightHanded = true, horizontalWidth = true;
        for (const auto& d : dirs) {
            const Vec3 axis{d[0], d[1], d[2]};
            const auto f = profileSectionFrame(axis);
            const double axisLen = lengthOf(axis);
            if (!near(lengthOf(f.width), 1.0, 1e-9) || !near(lengthOf(f.height), 1.0, 1e-9) ||
                !near(lengthOf(f.axis), 1.0, 1e-9)) {
                orthonormal = false;
            }
            if (!near(dotOf(f.width, f.height), 0.0, 1e-9) ||
                !near(dotOf(f.width, f.axis), 0.0, 1e-9) ||
                !near(dotOf(f.height, f.axis), 0.0, 1e-9)) {
                orthonormal = false;
            }
            const Vec3 expectedAxis{axis.x / axisLen, axis.y / axisLen, axis.z / axisLen};
            if (!nearV(f.axis, expectedAxis, 1e-9)) rightHanded = false;
            if (!nearV(crossOf(f.width, f.height), expectedAxis, 1e-9)) rightHanded = false;
            // Dusey olmayan her uyede genislik ekseni yatay kalir → baslik
            // plakasi en azindan plana gore yatay oturur.
            if (std::abs(expectedAxis.z) < 0.99 && !near(f.width.z, 0.0, 1e-12)) horizontalWidth = false;
        }
        check(orthonormal, "tum yonlerde cerceve ortonormal");
        check(rightHanded, "tum yonlerde sag elli (Z = X ^ Y)");
        check(horizontalWidth, "dusey olmayan tum uyelerde genislik ekseni yatay");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
