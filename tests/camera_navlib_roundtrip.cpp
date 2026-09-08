#include "model_maker/camera.hpp"
#include <cstdio>
#include <cmath>

static bool close(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    mm::Camera cam;
    cam.setView(mm::StandardView::Isometric);
    cam.setCenter3D({1200.0, 700.0, 300.0});

    auto m = cam.cameraToWorldMatrix4();
    // Navlib mimarisi: kamera BURADAN yeni matrisle doner. Ayni matrisi geri
    // uygulayinca ayni duruma donmeli (round-trip tersinden bagimsiz).
    cam.applyCameraToWorldMatrix4(m);

    // Isometric, apply alinca useIso=false olur; yaw/pitch/roll olarak cozulur.
    double yaw = cam.yaw(), pitch = cam.pitch(), roll = cam.roll();
    auto c = cam.center3D();
    printf("yaw=%.9f pitch=%.9f roll=%.9f c=(%.3f,%.3f,%.3f)\n", yaw, pitch, roll, c.x, c.y, c.z);

    // Beklenen: Isometric'in matrisinden cozulen yaw/pitch/roll ~0 (iso degil ama
    // apply sonrasi useIso false ve acilar ~0'a yakin olmali). Center korunmali.
    bool ok = close(c.x, 1200.0) && close(c.y, 700.0) && close(c.z, 300.0);
    printf("center preserved: %s\n", ok ? "OK" : "FAIL");

    // Ikinci: bilinçli yaw/pitch ile dogrudan round-trip.
    mm::Camera c2;
    c2.setView(mm::StandardView::Top);
    c2.rotate(0.6, 0.3);
    c2.setCenter3D({100.0, 200.0, 50.0});
    // Kaydet
    double y0 = c2.yaw(), p0 = c2.pitch(), r0 = c2.roll();
    auto z0 = c2.center3D();
    auto m2 = c2.cameraToWorldMatrix4();
    c2.applyCameraToWorldMatrix4(m2);
    bool ok2 = close(c2.yaw(), y0) && close(c2.pitch(), p0) && close(c2.roll(), r0) &&
               close(c2.center3D().x, z0.x) && close(c2.center3D().y, z0.y) && close(c2.center3D().z, z0.z);
    printf("explicit yaw/pitch round-trip: %s (yaw %.6f->%.6f pitch %.6f->%.6f)\n",
           ok2 ? "OK" : "FAIL", y0, c2.yaw(), p0, c2.pitch());

    return (ok && ok2) ? 0 : 1;
}
