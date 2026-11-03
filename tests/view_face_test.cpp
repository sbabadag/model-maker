// YUZEYE GORE GORUNUS — cekirdek test (OCC/Qt/windows.h YOK, Linux'ta kosar).
//
// Kullanici sarti: yuzeye tiklayinca o yuzun duzlemine dik yeni bir gorunus
// acilir; derinlik varsayilani 100/100 mm. Cerceve UCS'nin yuz kuraliyla AYNI
// olmali (X = uye ekseninin yuz duzlemine izdusumu, Y = N x X, izleyici
// tarafi = N disa bakan normal) — aksi halde gorunus yuzle ayni duzlemde olmaz.
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
static bool approx(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }
static double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static double len(const Vec3& a) { return std::sqrt(dot(a, a)); }

int main() {
    // 1) Duvar yuzu (normal +X), kiris Y boyunca: X = +Y, Y = Z, izleyici +X.
    {
        const ViewDefinition v = viewFromFace(Vec3{100, 0, 500}, Vec3{1, 0, 0}, Vec3{0, 1, 0});
        check(approx(v.origin.x, 100) && approx(v.origin.z, 500), "1 origin tiklanan nokta");
        check(approx(v.normal.x, 1) && approx(v.normal.y, 0) && approx(v.normal.z, 0),
              "1 normal = disa bakan yuz normali (+X)");
        check(approx(v.right.x, 0) && approx(v.right.y, 1), "1 right = uye ekseni izdusumu (+Y)");
        check(approx(v.up.z, 1), "1 up = N x X (+Z)");
        check(approx(v.depthFront, 100.0) && approx(v.depthBack, 100.0),
              "1 varsayilan derinlik 100/100 mm");
        check(!isPlanView(v), "1 duvar gorunusu plan degil");
        // sag-el: normal = right x up
        const Vec3 rxu{v.right.y * v.up.z - v.right.z * v.up.y,
                       v.right.z * v.up.x - v.right.x * v.up.z,
                       v.right.x * v.up.y - v.right.y * v.up.x};
        check(approx(rxu.x, v.normal.x) && approx(rxu.y, v.normal.y) && approx(rxu.z, v.normal.z),
              "1 sag-el ortonormal (normal = right x up)");
    }
    // 2) Ust flans yuzu (normal +Z), kiris X boyunca: right = +X, up = +Y, plan-benzeri.
    {
        const ViewDefinition v = viewFromFace(Vec3{0, 0, 300}, Vec3{0, 0, 1}, Vec3{1, 0, 0});
        check(approx(v.right.x, 1) && approx(v.right.z, 0), "2 right = +X");
        check(approx(v.up.y, 1), "2 up = +Y");
        check(approx(v.normal.z, 1), "2 normal = +Z (yukaridan bakis)");
        check(isPlanView(v), "2 yatay yuz gorunusu plan sayilir");
        check(approx(len(v.right), 1.0) && approx(len(v.up), 1.0) && approx(len(v.normal), 1.0),
              "2 eksenler birim");
        check(approx(dot(v.right, v.up), 0) && approx(dot(v.right, v.normal), 0) &&
                  approx(dot(v.up, v.normal), 0),
              "2 eksenler karsilikli dik");
    }
    // 3) UC yuzu: uye ekseni normale PARALEL — right yatay dusmeli, dejenere olmamali.
    {
        const ViewDefinition v = viewFromFace(Vec3{2000, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 0, 0});
        check(approx(len(v.right), 1.0) && approx(len(v.up), 1.0), "3 uc yuzunde eksenler birim");
        check(approx(dot(v.right, v.normal), 0) && approx(dot(v.up, v.normal), 0),
              "3 uc yuzunde X/Y normale dik (duzlem dogru)");
        check(approx(v.depthFront, 100.0), "3 uc yuzunde de 100 mm derinlik");
    }
    // 4) Sifir normal (bozuk yuz): cokme yok, dunya cercevesi + 100 mm.
    {
        const ViewDefinition v = viewFromFace(Vec3{5, 6, 7}, Vec3{0, 0, 0}, Vec3{0, 0, 0});
        check(approx(len(v.right), 1.0) && approx(len(v.up), 1.0) && approx(len(v.normal), 1.0),
              "4 dejenere normalde guvenli dunya cercevesi");
        check(approx(v.depthFront, 100.0) && approx(v.depthBack, 100.0), "4 derinlik yine 100/100");
    }
    // 5) Acik derinlik verilirse korunur; negatif/NaN 100'e duser.
    {
        const ViewDefinition v1 = viewFromFace(Vec3{}, Vec3{0, 1, 0}, Vec3{1, 0, 0}, 250.0, 60.0);
        check(approx(v1.depthFront, 250.0) && approx(v1.depthBack, 60.0),
              "5 acik derinlik degerleri korunur");
        const ViewDefinition v2 = viewFromFace(Vec3{}, Vec3{0, 1, 0}, Vec3{1, 0, 0}, -5.0,
                                               std::nan(""));
        check(approx(v2.depthFront, 100.0) && approx(v2.depthBack, 100.0),
              "5 gecersiz derinlik varsayilana duser");
    }
    // 6) Eksen hizalama: right, member axis'in yuz duzlemine izdusumu olmali.
    {
        // Egik uye ekseni (1,1,0), yuz normali +Z: right = normalize(1,1,0).
        const ViewDefinition v = viewFromFace(Vec3{}, Vec3{0, 0, 1}, Vec3{1, 1, 0});
        const double inv = 1.0 / std::sqrt(2.0);
        check(approx(v.right.x, inv) && approx(v.right.y, inv),
              "6 right = uye ekseninin izdusumu (normalize)");
    }

    std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
