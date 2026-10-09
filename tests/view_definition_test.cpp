// Iki noktali gorunus (Tekla "Create view by two points") cekirdek testi.
#include "model_maker/camera.hpp"
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

int main() {
    // 1) Yatay iki nokta: sag = +X, yukari = +Z, bakis XY'ye paralel.
    const auto v = viewFromTwoPoints({0, 0, 0}, {6000, 0, 0});
    check(v.has_value(), "two horizontal points -> view");
    check(nearV(v->right, {1, 0, 0}), "right axis = P1->P2 direction");
    check(nearV(v->up, {0, 0, 1}), "up axis = world Z (view perpendicular to XY)");
    check(near(v->normal.z, 0.0), "view direction horizontal (parallel to XY)");
    check(near(v->right.x * v->normal.x + v->right.y * v->normal.y + v->right.z * v->normal.z, 0.0),
          "normal orthogonal to right");

    // 2) Kamerada: P1 solda, P2 sagda, yuksek Z yukarida, iki nokta ayni ekran yuksekliginde.
    Camera cam;
    cam.setViewBasis(v->right, v->up, v->origin);
    const int W = 800, H = 600;
    const Vec2 s1 = cam.project({0, 0, 0}, W, H), s2 = cam.project({6000, 0, 0}, W, H);
    const Vec2 top = cam.project({0, 0, 3000}, W, H);
    check(s1.x < s2.x, "P1 left of P2 on screen");
    check(near(s1.y, s2.y), "P1 and P2 at same screen height");
    check(top.y < s1.y, "higher Z is drawn higher (not upside down)");
    const Vec2 depthA = cam.project({3000, -500, 1000}, W, H), depthB = cam.project({3000, 900, 1000}, W, H);
    check(near(depthA.x, depthB.x) && near(depthA.y, depthB.y), "depth along normal does not move screen point");

    // 3) Egik (diyagonal) iki nokta + Z farki: Z farki gorunusu egmez, yalniz XY yonu kullanilir.
    const auto d = viewFromTwoPoints({1000, 1000, 0}, {4000, 5000, 2500});
    check(d.has_value(), "diagonal points with Z difference -> view");
    check(nearV(d->right, {0.6, 0.8, 0}), "right = horizontal projection of P1->P2");
    check(nearV(d->up, {0, 0, 1}), "Z difference ignored: up stays world Z");
    Camera dc; dc.setViewBasis(d->right, d->up, d->origin);
    const Vec2 a = dc.project({1000, 1000, 0}, W, H), b = dc.project({4000, 5000, 0}, W, H);
    check(near(a.y, b.y) && a.x < b.x, "diagonal picks project to a horizontal screen line");

    // 4) Gecersiz: ayni nokta / dusey (XY'de ayni) -> gorunus yok.
    check(!viewFromTwoPoints({1, 2, 3}, {1, 2, 3}).has_value(), "same point rejected");
    check(!viewFromTwoPoints({1, 2, 0}, {1, 2, 5000}).has_value(), "vertical pair (same XY) rejected");

    // 5) Derinlik dilimi: varsayilan on/arka derinlik icindekiler gorunur.
    const auto s = viewFromTwoPoints({0, 0, 0}, {6000, 0, 0}, 1000.0, 1000.0);
    check(boundsInViewSlab({{0, -200, 0}, {6000, 200, 300}}, *s), "beam on the view line inside slab");
    check(!boundsInViewSlab({{0, 5000, 0}, {6000, 5400, 300}}, *s), "beam 5 m behind outside slab");
    check(!boundsInViewSlab({{0, -5400, 0}, {6000, -5000, 300}}, *s), "beam 5 m in front outside slab");
    check(boundsInViewSlab({{-100, -4000, 0}, {100, 4000, 300}}, *s), "crossing beam intersects slab");
    const auto deep = viewFromTwoPoints({0, 0, 0}, {6000, 0, 0}, 6000.0, 6000.0);
    check(boundsInViewSlab({{0, 5000, 0}, {6000, 5400, 300}}, *deep), "larger depth includes it");
    check(std::isfinite(s->depthFront) && std::isfinite(s->depthBack), "finite depths");

    // 6) Gecersiz derinlik (negatif/NaN) guvenli varsayilana kelepcelenir.
    const auto bad = viewFromTwoPoints({0, 0, 0}, {1, 0, 0}, -5.0, std::nan(""));
    check(bad && bad->depthFront >= 0.0 && bad->depthBack >= 0.0 && std::isfinite(bad->depthBack),
          "invalid depths sanitized");

    // 7) setViewBasis ortonormal: projeksiyon olcegi eksenlerde ayni.
    Camera oc; oc.setViewBasis({3, 4, 0}, {0, 0, 2}, {0, 0, 0});
    const Vec2 o = oc.project({0, 0, 0}, W, H), ox = oc.project({0.6, 0.8, 0}, W, H),
               oz = oc.project({0, 0, 1}, W, H);
    check(near(std::hypot(ox.x - o.x, ox.y - o.y), std::hypot(oz.x - o.x, oz.y - o.y), 1e-6),
          "basis normalized (equal scale on right/up)");

    // 8) Sigdirma kutusu: yalniz dilimdekiler; NaN guvenli; bos dilim bos ekran degil.
    const std::vector<Bounds3> beams{{{0, -100, 0}, {6000, 100, 400}},       // dilimde
                                     {{0, 8000, -9000}, {6000, 8200, 9000}}, // dilim disi (arkada)
                                     {{std::nan(""), 0, 0}, {1, 1, 1}}};     // bozuk
    const Bounds3 fit = viewFitBounds(beams, *s);
    check(near(fit.minimum.z, 0.0) && near(fit.maximum.z, 400.0), "fit uses only beams inside slab");
    check(std::isfinite(fit.minimum.x) && std::isfinite(fit.maximum.y), "NaN bounds ignored");
    const Bounds3 empty = viewFitBounds({}, *s);
    check(empty.maximum.z - empty.minimum.z > 1000.0 && near(empty.maximum.x - empty.minimum.x, 6000.0),
          "empty slab -> non-degenerate box around P1-P2");
    Camera fc; fc.setViewBasis(s->right, s->up, s->origin);
    check(fc.fit3D(fit.minimum, fit.maximum, W, H, 40.0), "camera can fit the view box");
    const Vec2 f1 = fc.project({0, 0, 0}, W, H), f2 = fc.project({6000, 0, 400}, W, H);
    check(f1.x >= 39.0 && f2.x <= W - 39.0 && f2.y >= 0.0 && f1.y <= H, "fitted beam on screen");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
