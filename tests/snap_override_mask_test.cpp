// SNAP OVERRIDE MASKESI regresyon testi.
// Sag-tik popup menusu snap turlerini acip kapatir (Application::toggleSnapType).
// Bu test, maskenin GERCEKTEN snap sonucunu degistirdigini olcer: ayni imlec
// konumunda yalniz Endpoint acikken Endpoint, kapatildiginda Endpoint DISI.
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/camera.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
    else std::printf("ok   %s\n", what);
}
}  // namespace

int main() {
    Document doc;
    {
        EntityProperties p;
        p.profileName = "BOX100";
        p.axisFromX = 0; p.axisFromY = 0; p.axisFromZ = 0;
        p.axisToX = 4000; p.axisToY = 0; p.axisToZ = 0;
        std::vector<Vec3> v;
        for (double x : {0.0, 4000.0}) {
            v.push_back({x, -50, -50}); v.push_back({x, 50, -50});
            v.push_back({x, 50, 50});   v.push_back({x, -50, 50});
        }
        std::vector<Edge> e{{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                            {0,4},{1,5},{2,6},{3,7}};
        WireframeModel m(v, e);
        m.setProperties(p);
        doc.addModel(std::move(m));
    }

    Camera cam;
    cam.setView(StandardView::Top);
    cam.setCenter3D(Vec3{2000, 0, 0});
    cam.fit3D(Vec3{0, -200, -200}, Vec3{4000, 200, 200}, 1200, 900, 20.0);

    const int W = 1200, H = 900;
    const WorkPlane plane{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    // Ust flans kosesine 2 px uzakta imlec
    const Vec3 corner{4000, 50, 50};
    const Vec2 px = cam.project(corner, W, H);
    const Vec2 cursor{px.x + 2.0, px.y + 2.0};

    SnapTypeMask allOn; allOn.fill(true);
    const auto rAll = SnapEngine::snap3D(cursor, doc, cam, W, H, 10.0, plane,
                                         true, false, std::nullopt, &allOn, false);
    std::printf("tum turler acik  -> tip=%d (%.1f,%.1f,%.1f)\n", (int)rAll.type,
                rAll.point.x, rAll.point.y, rAll.point.z);
    check(rAll.type != SnapType::None, "tum turler acikken snap bulunuyor");

    SnapTypeMask onlyEndpoint; onlyEndpoint.fill(false);
    onlyEndpoint[static_cast<std::size_t>(SnapType::Endpoint)] = true;
    const auto rEp = SnapEngine::snap3D(cursor, doc, cam, W, H, 10.0, plane,
                                        true, false, std::nullopt, &onlyEndpoint, false);
    std::printf("yalniz Endpoint   -> tip=%d (%.1f,%.1f,%.1f)\n", (int)rEp.type,
                rEp.point.x, rEp.point.y, rEp.point.z);
    check(rEp.type == SnapType::Endpoint, "yalniz Endpoint acikken Endpoint donuyor");

    // Endpoint kapatilir (popup'ta isareti kaldirmak) -> ayni nokta Endpoint olmamali
    SnapTypeMask noEndpoint = allOn;
    noEndpoint[static_cast<std::size_t>(SnapType::Endpoint)] = false;
    const auto rNo = SnapEngine::snap3D(cursor, doc, cam, W, H, 10.0, plane,
                                        true, false, std::nullopt, &noEndpoint, false);
    std::printf("Endpoint kapali   -> tip=%d (%.1f,%.1f,%.1f)\n", (int)rNo.type,
                rNo.point.x, rNo.point.y, rNo.point.z);
    check(rNo.type != SnapType::Endpoint,
          "Endpoint kapatildiginda ayni konumda Endpoint DONMUYOR (override calisiyor)");

    // Hicbir tur + grid kapali -> None
    SnapTypeMask noneMask; noneMask.fill(false);
    const auto rNone = SnapEngine::snap3D(cursor, doc, cam, W, H, 10.0, plane,
                                          true, false, std::nullopt, &noneMask, false);
    std::printf("tum turler kapali -> tip=%d\n", (int)rNone.type);
    check(rNone.type == SnapType::None, "tum turler kapaliyken snap yok");

    std::printf(failures == 0 ? "TUM TESTLER GECTI\n" : "%d TEST BASARISIZ\n", failures);
    return failures == 0 ? 0 : 1;
}
