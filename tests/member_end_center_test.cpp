// UYE UC MERKEZI (grip tutamagi) regresyon testi.
// Selahattin 2026-10-31: "uc noktadaki tasima noktasi her zaman x ve y de tam
// orta noktada durmali". Top/Bottom ankrajli profilde EKSEN kesitin basligindan
// gecer; tutamak eksen ucunda durunca kesitin kenarinda kaliyordu.
#include "model_maker/geometry.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
    else std::printf("ok   %s\n", what);
}
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// Dikdortgen kesitli dikey uye: kesit x[-115,115] y[-120,120], eksen x=+115
// uzerinde (Top ankraji gibi kesitin yuzunde), z=0..3000.
WireframeModel makeTopAnchoredColumn() {
    EntityProperties p;
    p.profileName = "HEA240";
    p.axisFromX = 115.0; p.axisFromY = 0.0; p.axisFromZ = 0.0;
    p.axisToX = 115.0;   p.axisToY = 0.0;   p.axisToZ = 3000.0;

    std::vector<Vec3> verts;
    for (double z : {0.0, 3000.0}) {
        verts.push_back(Vec3{-115.0, -120.0, z});
        verts.push_back(Vec3{ 115.0, -120.0, z});
        verts.push_back(Vec3{ 115.0,  120.0, z});
        verts.push_back(Vec3{-115.0,  120.0, z});
    }
    std::vector<Edge> edges;
    for (std::size_t i = 0; i < verts.size(); ++i) {
        // iki ucta da kapali halka + dusey kenarlar
        edges.push_back({static_cast<int>(i), static_cast<int>((i + 1) % 4 + (i < 4 ? 0 : 4))});
    }
    WireframeModel model(std::move(verts), std::move(edges));
    model.setProperties(p);
    return model;
}

}  // namespace

int main() {
    const WireframeModel column = makeTopAnchoredColumn();

    const Vec3 bottom = memberEndCenter(column, false);
    const Vec3 top = memberEndCenter(column, true);
    std::printf("alt  merkez = (%.2f, %.2f, %.2f)\n", bottom.x, bottom.y, bottom.z);
    std::printf("ust  merkez = (%.2f, %.2f, %.2f)\n", top.x, top.y, top.z);

    check(near(bottom.x, 0.0, 0.5), "alt uc merkezi kesit ortasinda (x=0)");
    check(near(bottom.y, 0.0, 0.5), "alt uc merkezi kesit ortasinda (y=0)");
    check(near(bottom.z, 0.0, 0.5), "alt uc merkezi alt duzlemde");
    check(near(top.x, 0.0, 0.5), "ust uc merkezi kesit ortasinda (x=0)");
    check(near(top.y, 0.0, 0.5), "ust uc merkezi kesit ortasinda (y=0)");
    check(near(top.z, 3000.0, 0.5), "ust uc merkezi ust duzlemde");
    check(!near(bottom.x, 115.0, 1.0), "tutamak eksen ucunda DEGIL (eski hata)");

    // Dejenere eksen: eksen ucuna duser.
    EntityProperties p;
    p.axisFromX = 10.0; p.axisFromZ = 20.0;
    p.axisToX = 10.0;   p.axisToZ = 20.0;
    WireframeModel degenerate;
    degenerate.setProperties(p);
    const Vec3 d = memberEndCenter(degenerate, false);
    check(near(d.x, 10.0, 1e-6) && near(d.z, 20.0, 1e-6), "dejenere eksende eksen ucuna duser");

    // Uc duzleminde koke yoksa guvenli geri donus.
    EntityProperties q;
    q.axisFromX = 0.0; q.axisFromZ = 0.0; q.axisToX = 0.0; q.axisToZ = 1000.0;
    WireframeModel empty({Vec3{500.0, 0.0, 500.0}}, {});
    empty.setProperties(q);
    const Vec3 e = memberEndCenter(empty, true);
    check(near(e.z, 1000.0, 1e-6), "koke yoksa eksen ucuna doner");

    std::printf(failures == 0 ? "TUM TESTLER GECTI\n" : "%d TEST BASARISIZ\n", failures);
    return failures == 0 ? 0 : 1;
}
