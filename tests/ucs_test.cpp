// TEKLA-TARZI UCS + VIEW SISTEMI (P1) — cekirdek test.
//
// Kapsam: global XY/XZ/YZ calisma duzlemi, 1/2/3 noktadan UCS, B-Rep
// yuzeyinden UCS, parca koordinat sisteminden UCS, UCS->WorkPlane koprusu,
// yerel/dunya donusumleri, UCS uzerinde view, 2/3 noktadan view, yuzeye dik
// view, named views + view list, global UCS'ye donus.
//
// OCC'siz, Linux'ta kosar. Tum govdeler src/ucs.cpp'deki GERCEK fonksiyonlar.

#include "model_maker/ucs.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static bool near3(const Vec3& a, const Vec3& b, double tol = 1e-9) {
    return std::abs(a.x - b.x) < tol && std::abs(a.y - b.y) < tol && std::abs(a.z - b.z) < tol;
}

static double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static double len(const Vec3& v) { return std::sqrt(dot(v, v)); }

int main() {
    // 1) Global + world.
    {
        const Ucs w = Ucs::world();
        check(w.isValid(), "world gecerli");
        check(near3(w.origin, {}), "world origin = 0");
        check(near3(w.xAxis, {1, 0, 0}) && near3(w.yAxis, {0, 1, 0}) && near3(w.zAxis, {0, 0, 1}),
              "world eksenleri identity");
    }

    // 2) Global calisma duzlemleri (XY/XZ/YZ).
    {
        const Vec3 o{10, 20, 30};
        check(Ucs::planeXY(o).isValid(), "planeXY gecerli");
        check(near3(Ucs::planeXY(o).zAxis, {0, 0, 1}), "planeXY normal +Z");
        check(Ucs::planeXZ(o).isValid(), "planeXZ gecerli");
        check(near3(Ucs::planeXZ(o).zAxis, {0, -1, 0}), "planeXZ normal -Y");
        check(near3(Ucs::planeXZ(o).yAxis, {0, 0, 1}), "planeXZ yukari +Z");
        check(Ucs::planeYZ(o).isValid(), "planeYZ gecerli");
        check(near3(Ucs::planeYZ(o).zAxis, {1, 0, 0}), "planeYZ normal +X");
        check(near3(Ucs::planeYZ(o).yAxis, {0, 0, 1}), "planeYZ yukari +Z");
        check(near3(Ucs::planeXY(o).origin, o), "plane origin tasinir");
    }

    // 3) 1 noktadan UCS: origin, eksenler dunya.
    {
        const Ucs u = Ucs::fromOrigin({5, -3, 9});
        check(near3(u.origin, {5, -3, 9}), "fromOrigin origin");
        check(near3(u.xAxis, {1, 0, 0}), "fromOrigin X dunya");
    }

    // 4) 2 noktadan UCS: origin + X yonu, Z dunya.
    {
        const auto u = Ucs::fromOriginAndX({0, 0, 0}, {0, 10, 0}); // X = +Y dunya
        check(u.has_value(), "2-pt: gecerli");
        if (u) {
            check(near3(u->xAxis, {0, 1, 0}), "2-pt X = verilen yon");
            check(near3(u->zAxis, {0, 0, 1}), "2-pt Z = dunya Z");
            check(u->isValid(), "2-pt ortonormal");
        }
        // Dejenere: X yonu Z'ye paralel.
        check(!Ucs::fromOriginAndX({0, 0, 0}, {0, 0, 5}).has_value(), "2-pt: Z'ye paralel X gecersiz");
        check(!Ucs::fromOriginAndX({0, 0, 0}, {0, 0, 0}).has_value(), "2-pt: cakisan gecersiz");
    }

    // 5) 3 noktadan UCS.
    {
        const auto u = Ucs::fromThreePoints({1, 2, 3}, {4, 2, 3}, {1, 2, 6}); // X=+X, Y=+Z
        check(u.has_value(), "3-pt: gecerli");
        if (u) {
            check(near3(u->origin, {1, 2, 3}), "3-pt origin");
            check(near3(u->xAxis, {1, 0, 0}), "3-pt X yonu");
            check(near3(u->zAxis, {0, -1, 0}), "3-pt Z = X x Y yonu");
            check(u->isValid(), "3-pt ortonormal sag-el");
        }
        check(!Ucs::fromThreePoints({0, 0, 0}, {0, 0, 0}, {1, 0, 0}).has_value(),
              "3-pt: cakisan origin/x gecersiz");
    }

    // 6) B-Rep yuzeyinden UCS: zAxis = normal.
    {
        const Ucs u = Ucs::fromPlaneAndNormal({7, 8, 9}, {0, 0, 1});
        check(near3(u.zAxis, {0, 0, 1}), "yuzey UCS: zAxis = normal");
        check(u.isValid(), "yuzey UCS gecerli");
        // Egitik normal.
        const Ucs t = Ucs::fromPlaneAndNormal({0, 0, 0}, {1, 0, 1});
        check(std::abs(dot(t.zAxis, {1, 0, 1}) / len({1, 0, 1}) - 1.0) < 1e-9,
              "yuzey UCS: egitik normal normalize edilir");
    }

    // 7) Parca koordinat sisteminden UCS (eksen + referans normali).
    {
        // Yatay uye (+X ekseni), baslik dunya XY'ye paralel (normal +Z).
        const Ucs u = Ucs::fromPartFrame({0, 0, 0}, {1, 0, 0});
        check(near3(u.zAxis, {1, 0, 0}), "part UCS: zAxis = uye ekseni");
        check(u.isValid(), "part UCS gecerli");
        // Baslik normali (X yonu) dunya XY'ye paralel olmali: X . Z == 0.
        check(std::abs(u.xAxis.z) < 1e-9, "part UCS: baslik XY'ye paralel (yatay uye)");
    }

    // 8) UCS <-> WorkPlane koprusu.
    {
        const Ucs u = Ucs::fromThreePoints({1, 1, 1}, {2, 1, 1}, {1, 1, 2}).value_or(Ucs{});
        const WorkPlane wp = u.toWorkPlane();
        check(near3(wp.origin, u.origin), "toWorkPlane origin");
        check(near3(wp.u, u.xAxis) && near3(wp.v, u.yAxis) && near3(wp.normal, u.zAxis),
              "toWorkPlane eksen esleme");
        const Ucs back = Ucs::fromWorkPlane(wp);
        check(near3(back.xAxis, u.xAxis) && near3(back.yAxis, u.yAxis) && near3(back.zAxis, u.zAxis),
              "fromWorkPlane round-trip");
    }

    // 9) Yerel/dunya donusum round-trip.
    {
        const auto u = Ucs::fromThreePoints({1, 2, 3}, {1, 5, 3}, {1, 2, 4});
        check(u.has_value(), "yerel/dunya: 3-pt UCS gecerli");
        if (u && u->isValid()) {
            const Vec3 world{9.0, -4.0, 7.0};
            const Vec3 local = u->toLocal(world);
            const Vec3 back = u->toWorld(local);
            check(near3(world, back, 1e-9), "toLocal/toWorld round-trip");
        } else {
            check(false, "yerel/dunya: UCS gecerli olmali");
        }
        // Origin -> local 0.
        const Ucs w = Ucs::world();
        check(near3(w.toLocal({5, 6, 7}), {5, 6, 7}), "world toLocal identity");
    }

    // 10) UCS uzerinde view.
    {
        const Ucs u = Ucs::planeXZ({0, 0, 0});
        const UcsView v = viewFromUcs(u);
        check(near3(v.right, u.xAxis), "viewFromUcs right = xAxis");
        check(near3(v.up, u.yAxis), "viewFromUcs up = yAxis");
        check(near3(v.normal, u.zAxis), "viewFromUcs normal = zAxis");
        check(near3(v.normal, cross(v.right, v.up)), "viewFromUcs right x up = normal");
    }

    // 11) Yuzeye dik view.
    {
        const UcsView v = viewPerpendicularToFace({0, 0, 0}, {0, 0, 1});
        check(near3(v.normal, {0, 0, 1}), "dik view normal = yuzey normali");
        check(near3(cross(v.right, v.up), v.normal), "dik view right x up = normal");
        // Normal Z'ye paralelken up = Y.
        check(near3(v.up, {0, 1, 0}), "dik view (normal +Z) up = +Y");
    }

    // 12) 2 / 3 noktadan view.
    {
        const auto v2 = viewFromTwoPointsUcs({0, 0, 0}, {10, 0, 0});
        check(v2.has_value(), "2-pt view gecerli");
        if (v2) {
            check(near3(v2->right, {1, 0, 0}), "2-pt view right = +X");
            check(near3(v2->up, {0, 0, 1}), "2-pt view up = +Z");
        }
        check(!viewFromTwoPointsUcs({0, 0, 0}, {0, 0, 5}).has_value(), "2-pt view dusey gecersiz");

        const auto v3 = viewFromThreePointsUcs({0, 0, 0}, {1, 0, 0}, {0, 0, 1});
        check(v3.has_value(), "3-pt view gecerli");
        if (v3) check(near3(v3->normal, cross(v3->right, v3->up)), "3-pt view sag-el");
    }

    // 13) Named views + view list.
    {
        NamedViewList list;
        check(list.empty() && list.size() == 0, "liste baslangicta bos");
        NamedView a; a.name = L"Plan";
        NamedView b; b.name = L"Cephe";
        check(list.add(a), "ekle Plan");
        check(list.add(b), "ekle Cephe");
        check(list.size() == 2, "boyut 2");
        check(list.contains(L"Plan"), "contains Plan");
        check(list.find(L"Cephe") != nullptr, "find Cephe");
        check(list.find(L"Yok") == nullptr, "find yok -> nullptr");
        // Overwrite.
        NamedView a2; a2.name = L"Plan"; a2.origin = {1, 2, 3};
        check(list.add(a2), "overwrite Plan");
        check(list.size() == 2, "overwrite boyutu artirmaz");
        check(near3(list.find(L"Plan")->origin, {1, 2, 3}), "overwrite degeri gunceller");
        // Bos isim reddedilir.
        NamedView empty; check(!list.add(empty), "bos isim reddedilir");
        // Isim sirali liste.
        const auto names = list.names();
        check(names.size() == 2 && names[0] == L"Cephe" && names[1] == L"Plan",
              "names isim sirali");
        // Sil + temizle.
        check(list.remove(L"Plan"), "remove Plan");
        check(list.size() == 1 && !list.contains(L"Plan"), "remove sonrasi boyut");
        check(!list.remove(L"Plan"), "remove yok -> false");
        list.clear();
        check(list.empty(), "clear bosaltir");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
