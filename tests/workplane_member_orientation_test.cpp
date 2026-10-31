// İŞ DÜZLEMİ (UCS) — eğik düzlemde atılan profilin KESİTİ düzlemde kalmalı.
//
// Selahattin 2026-10-31: "3 nokta ile çalışma düzlemi belirlendiğinde yeni
// atılan profiller açıya uymuyor, world UCS'inde atılıyor; önceden çalışıyordu".
//
// Kök neden: yeni üyenin kesit referans normali varsayılan (0,0,1) kalıyordu.
// profileSectionFrame(axis, ref) kesitin genişlik/yükseklik yönlerini
// cross(ref, axis) ile kurar; ref eğik iş düzleminin NORMALİ olmalı ki kesit
// düzlem içinde kalsın. Bu dosya çekirdek sözleşmeyi kilitler:
//   ref = iş düzlemi normali  -> kesit yönleri düzleme PARALEL (dot ~ 0)
//   ref = dünya Z (ESKİ HAL)  -> kesit düzlemden çıkar (dot != 0)
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
static bool near0(double v, double eps = 1e-6) { return std::fabs(v) <= eps; }
static double dot3(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

int main() {
    // 3 noktayla kurulmuş eğik iş düzlemi: XY'de 45° eğik.
    const auto plane = WorkPlane::fromThreePoints({0.0, 0.0, 0.0},
                                                  {1000.0, 0.0, 1000.0},
                                                  {0.0, 1000.0, 0.0});
    check(plane.has_value(), "3 noktadan is duzlemi kuruldu");
    if (!plane) { std::printf("ALL FAIL\n"); return 1; }
    const Vec3 n = plane->normal;
    const Vec3 u = plane->u;
    const Vec3 v = plane->v;
    std::printf("       normal=(%.3f %.3f %.3f)\n", n.x, n.y, n.z);
    check(near0(dot3(n, u)) && near0(dot3(n, v)) && near0(dot3(u, v)),
          "duzlem tabani ortonormal (u,v,n)");

    // (1) Düzlem İÇİNDE çizilen kiriş: eksen = u. Ref = düzlem normali.
    {
        const ProfileSectionFrame f = profileSectionFrame(u, n);
        // Kiriste govde derinligi (height) is duzleminden DISARI bakar — bu
        // NORMALDIR (profil notr eksen uzerine oturur). Duzlemle uyum icin
        // FLANS GENISLIGI (width) is duzlemi ICINDE kalmali.
        check(near0(dot3(f.width, n)),
              "kiris: flans genisligi is duzlemi ICINDE (ref = duzlem normali)");
        check(std::fabs(dot3(f.height, n)) > 0.99,
              "kiris: govde derinligi duzleme DIK (notr eksen uzerinde oturur)");
        check(near0(dot3(f.width, u)) && near0(dot3(f.height, u)),
              "kiris: kesit yonleri uyeye DIK (eksen u boyunca)");
        check(!near0(dot3(f.height, v), 1e-3) || !near0(dot3(f.width, v), 1e-3),
              "kiris: kesit duzlem-ici bir yone oturur (dejenere degil)");
    }

    // (2) Eski hatalı hal: ref = dünya Z -> kesit düzlemden çıkar.
    {
        const ProfileSectionFrame bad = profileSectionFrame(u, Vec3{0.0, 0.0, 1.0});
        const double outOfPlane = std::max(std::fabs(dot3(bad.width, n)),
                                           std::fabs(dot3(bad.height, n)));
        std::printf("       eski hal: kesit duzlemden cikma = %.3f\n", outOfPlane);
        check(outOfPlane > 0.1,
              "eski hal (ref=world Z) kesiti is duzleminden CIKARIR — regresyon kaniti");
    }

    // (3) Düzleme DİK üye (kolon): eksen = n, ref = düzlem-ici u.
    {
        const ProfileSectionFrame f = profileSectionFrame(n, u);
        check(near0(dot3(f.width, n)) && near0(dot3(f.height, n)),
              "kolon: kesit yonleri is duzlemi ICINDE (ref = duzlem-ici u)");
        check(std::fabs(dot3(f.height, u)) > 0.99,
              "kolon: kesit yuksekligi duzlem-ici u boyunca (deterministik cerceve)");
    }
    // (3b) Kolonda ref = düzlem normali (eski/yanlış): dejenere -> dünya X'e düşer.
    {
        const ProfileSectionFrame deg = profileSectionFrame(n, n);
        const double parallel = std::fabs(dot3(deg.width, n));
        std::printf("       kolon ref=normal: width.n = %.3f (dejenere)\n", parallel);
        check(parallel < 0.01 || parallel > 0.99,
              "kolon ref=normal dejenere (kesit duzleme oturmaz) — duzlem-ici u sart");
    }

    std::printf("%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
