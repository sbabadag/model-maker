// VARSAYILAN 3B GORUNUM YONELIMI (kullanici sarti: "xy yere dik gelmesin").
//
// Eski Camera::reset() = buildRotation(-0.55, 0.45, 0) ekran-yukarisini +Y
// yapiyordu -> XY duzlemi yere DIK (dusey) gorunuyordu. Dogrusu: XY yere
// PARALEL (yatay), Z yukari — standart Tekla/AutoCAD 3B model gorunumu.
// reset() ve setView(Isometric) AYNI acidan beslenmeli (tek kaynak kIso*).
//
// R_ private oldugu icin ekran-yukari ve bakis yonu viewTransform'dan
// yeniden kurulur: R_ = world->camera (row-major), row0 = camera-X (ekran
// saga), row1 = camera-Y (ekran yukari), row2 = camera-Z (izleyiciye dogru).
//   row1 = (viewTransform(e_x).y, viewTransform(e_y).y, viewTransform(e_z).y)
//   row2 = (viewTransform(e_x).z, viewTransform(e_y).z, viewTransform(e_z).z)
#include "model_maker/camera.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static void orientation(const Camera& camera, Vec3& screenUp, Vec3& viewDir) {
    const Vec3 rx = camera.viewTransform(Vec3{1.0, 0.0, 0.0});
    const Vec3 ry = camera.viewTransform(Vec3{0.0, 1.0, 0.0});
    const Vec3 rz = camera.viewTransform(Vec3{0.0, 0.0, 1.0});
    screenUp = {rx.y, ry.y, rz.y};
    viewDir = {rx.z, ry.z, rz.z};
}

static bool near3(const Vec3& a, const Vec3& b) {
    return std::abs(a.x - b.x) < 1e-12 && std::abs(a.y - b.y) < 1e-12 &&
           std::abs(a.z - b.z) < 1e-12;
}

int main() {
    Camera resetCam;
    resetCam.reset();
    Vec3 up{}, view{};
    orientation(resetCam, up, view);

    // XY yere paralel: ekran-yukarinin Z bileseni baskın ve POZITIF.
    check(std::abs(up.z) > std::abs(up.x) && std::abs(up.z) > std::abs(up.y),
          "reset(): ekran-yukari +Z baskin (XY yere paralel, dusey degil)");
    check(up.z > 0.0, "reset(): +Z yukari (bas asagi degil)");
    // Izleyici yukaridan bakar (bakis yonunun Z bileseni negatif).
    check(view.z < 0.0, "reset(): izleyici yukaridan bakar");

    // Tek kaynak: reset() ve "3D view" (Isometric) AYNI aci.
    Camera iso;
    iso.setView(StandardView::Isometric);
    Vec3 isoUp{}, isoView{};
    orientation(iso, isoUp, isoView);
    check(near3(up, isoUp) && near3(view, isoView),
          "reset() ve Isometric AYNI yonelim (varsayilan == 3D view)");

    // YON regresyonu: Y baskin olmamali (eski "xy yere dik" davranisi).
    check(!(std::abs(up.y) > std::abs(up.x) && std::abs(up.y) > std::abs(up.z)),
          "reset(): ekran-yukari Y baskin DEGIL (XY dusey degil)");

    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
