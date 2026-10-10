// IKI NOKTALI GORUNUS: secilen noktalar IS DUZLEMINE iz dusurulur — cekirdek
// testi (OCC GEREKTIRMEZ, Linux'ta da kosar).
//
// Kullanici sarti: "2 noktayla view aldik; tasimalar kopyalamalar vs projected
// noktalardan workplane uzerinden tasinacak". Uygulama bunu updateHover'da
// `workPlane_.projectPoint(...)` ile yapar (bkz. tests/run_view_workplane_move.py
// kaynak-sekil testi). Burada cekirdek ozellikler olculur:
//   1) iz dusum normal bilesenini atar (nokta duzleme oturur),
//   2) iz dusum EKRANDA noktayi OYNATMAZ (gorunus duzleminin normali = bakis
//      yonu) — kullanici imlecin altinda gordugu noktayi kullanir,
//   3) duzlem disi snap'lerde (farkli derinlikteki uye ucu) yer degistirme
//      duzlem ICINDE kalir; iz dusum olmadan derinlige kayar,
//   4) egik (capraz) iki noktali gorunuste de ayni garanti,
//   5) birim olmayan / bozuk normallerde guvenli.
#include "model_maker/camera.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/view_definition.hpp"

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
static double dotOf(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static double planeOffset(const WorkPlane& p, const Vec3& v) { return dotOf(v - p.origin, p.normal); }

int main() {
    const int W = 1200, H = 800;

    // --- 1) +X dogrultusunda iki noktali gorunus: is duzlemi y=0 ---
    const ViewDefinition def = *viewFromTwoPoints({0, 0, 0}, {6000, 0, 0});
    const WorkPlane plane = viewWorkPlane(def);
    std::printf("== iki noktali gorunus (P1=(0,0,0) -> P2=(6000,0,0)) ==\n");
    std::printf("  is duzlemi normal = (%.3f, %.3f, %.3f)\n", plane.normal.x, plane.normal.y,
                plane.normal.z);

    const Vec3 deep{500, -1200, 3000};   // baska derinlikteki uye ucu
    check(std::abs(planeOffset(plane, deep)) > 1000.0, "baslangicta nokta duzlem DISINDA (derinlik 1200)");
    const Vec3 projected = plane.projectPoint(deep);
    check(nearV(projected, Vec3{500, 0, 3000}, 1e-9), "projectPoint normal bilesenini atar -> (500,0,3000)");
    check(near(planeOffset(plane, projected), 0.0, 1e-9), "iz dusurulmus nokta duzlem UZERINDE");

    // --- 2) duzlemdeki nokta degismez (idempotent) ---
    const Vec3 inPlane{2000, 0, 1500};
    check(nearV(plane.projectPoint(inPlane), inPlane, 1e-9), "duzlemdeki nokta degismez (idempotent)");

    // --- 3) EKRAN DEGISMEZLIGI (gercek Camera) ---
    Camera cam;
    cam.setViewBasis(def.right, def.up, def.origin);
    cam.fit3D({0, -1200, 0}, {6000, 0, 3000}, W, H, 40.0);
    const Vec2 before = cam.project(deep, W, H);
    const Vec2 after = cam.project(projected, W, H);
    std::printf("  ekran: ham (%.4f, %.4f)  izdusurulmus (%.4f, %.4f)\n", before.x, before.y, after.x,
                after.y);
    check(near(before.x, after.x, 1e-6) && near(before.y, after.y, 1e-6),
          "iz dusum EKRANDA noktayi oynatmaz (imlec altindaki nokta korunur)");

    // --- 4) YER DEGISTIRME duzlem ICINDE kalir ---
    // Baz baska derinlikteki uca snap'li, hedef gorunus duzlemindeki serbest nokta.
    const Vec3 baseRaw = deep;                    // (500,-1200,3000)
    const Vec3 destRaw = {5500, 0, 1000};         // serbest: duzlemde
    const Vec3 rawDelta = destRaw - baseRaw;
    std::printf("  ham yer degistirme normal bileseni = %.3f\n", dotOf(rawDelta, plane.normal));
    check(!near(dotOf(rawDelta, plane.normal), 0.0, 1e-6),
          "iz dusum OLMADAN yer degistirme duzlemden CIKAR (derinlige kayar)");
    const Vec3 fixedDelta = plane.projectPoint(destRaw) - plane.projectPoint(baseRaw);
    std::printf("  izdusurulmus yer degistirme normal bileseni = %.3f\n",
                dotOf(fixedDelta, plane.normal));
    check(near(dotOf(fixedDelta, plane.normal), 0.0, 1e-9),
          "iz dusum ILE yer degistirme duzlem ICINDE (normal bileseni 0)");
    check(nearV(fixedDelta, Vec3{5000, 0, -2000}, 1e-9), "beklenen duzlem ici yer degistirme");

    // --- 5) Capraz (egik) iki noktali gorunus ---
    {
        const ViewDefinition diag = *viewFromTwoPoints({0, 0, 0}, {3000, 4000, 0});
        const WorkPlane dp = viewWorkPlane(diag);
        Camera dc;
        dc.setViewBasis(diag.right, diag.up, diag.origin);
        const Vec3 off{1000, 1000, 2500}; // duzlem disi (normal yonunde bilesenli)
        const Vec3 on = dp.projectPoint(off);
        check(near(planeOffset(dp, on), 0.0, 1e-9), "capraz gorunus: iz dusurulmus nokta duzlemde");
        const Vec2 s1 = dc.project(off, W, H);
        const Vec2 s2 = dc.project(on, W, H);
        check(near(s1.x, s2.x, 1e-6) && near(s1.y, s2.y, 1e-6),
              "capraz gorunus: iz dusum ekranda oynatmaz");
    }

    // --- 6) Birim olmayan normal ---
    {
        WorkPlane scaled = plane;
        scaled.normal = {0.0, -4.0, 0.0}; // birim degil, ayni yon
        check(nearV(scaled.projectPoint(deep), Vec3{500, 0, 3000}, 1e-9),
              "birim olmayan normal de dogru iz dusurur");
    }

    // --- 7) Bozuk (sifir) normal: dokunmaz, cokmez ---
    {
        WorkPlane broken = plane;
        broken.normal = {0.0, 0.0, 0.0};
        check(nearV(broken.projectPoint(deep), deep, 1e-12), "sifir normalde nokta degismez (guvenli)");
    }

    std::printf("\n%d kontrol, %d hata\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
