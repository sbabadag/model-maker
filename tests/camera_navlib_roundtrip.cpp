#include "model_maker/camera.hpp"
#include <cstdio>
#include <cmath>

static bool close(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

int main() {
    mm::Camera cam;
    cam.setView(mm::StandardView::Isometric);
    cam.setCenter3D({1200.0, 700.0, 300.0});

    auto m = cam.cameraToWorldMatrix4();
    // Navlib mimarisi: kamera BURADAN yeni matrisle doner. Ayni matrisi geri
    // uygulayinca ayni duruma donmeli (round-trip tersinden bagimsiz).
    cam.applyCameraToWorldMatrix4(m);

    double yaw = cam.yaw(), pitch = cam.pitch(), roll = cam.roll();
    auto c = cam.center3D();
    printf("yaw=%.9f pitch=%.9f roll=%.9f c=(%.3f,%.3f,%.3f)\n", yaw, pitch, roll, c.x, c.y, c.z);

    bool ok = close(c.x, 1200.0) && close(c.y, 700.0) && close(c.z, 300.0);
    printf("center preserved: %s\n", ok ? "OK" : "FAIL");

    // Ikinci: bilinçli yaw/pitch ile dogrudan round-trip.
    mm::Camera c2;
    c2.setView(mm::StandardView::Top);
    c2.rotate(0.6, 0.3);
    c2.setCenter3D({100.0, 200.0, 50.0});
    double y0 = c2.yaw(), p0 = c2.pitch(), r0 = c2.roll();
    auto z0 = c2.center3D();
    auto m2 = c2.cameraToWorldMatrix4();
    c2.applyCameraToWorldMatrix4(m2);
    bool ok2 = close(c2.yaw(), y0) && close(c2.pitch(), p0) && close(c2.roll(), r0) &&
               close(c2.center3D().x, z0.x) && close(c2.center3D().y, z0.y) && close(c2.center3D().z, z0.z);
    printf("explicit yaw/pitch round-trip: %s (yaw %.6f->%.6f pitch %.6f->%.6f)\n",
           ok2 ? "OK" : "FAIL", y0, c2.yaw(), p0, c2.pitch());

    // UCUNCU (yeni, kritik): kutup bolgesinden gecis — ROUNDTRIP SADAKATI.
    // Rotasyonun meşru hareketi yuzlerce px/adim olabilir; olculecek olan
    // rotate-sonrasi goruntu ile roundtrip-sonrasi goruntu arasindaki FARK
    // (0 olmali — kutupta bile). Eski euler kamerada ayristirma kutupta
    // baska bir matris uretir -> goruntu ziplardi.
    mm::Camera c3;
    c3.setView(mm::StandardView::Isometric);
    c3.setCenter3D({0.0, 0.0, 0.0});
    const mm::Vec3 p{3000.0, 2000.0, 1000.0};
    double maxRoundtripError = 0.0;
    bool fidelityOk = true;
    for (int step = 0; step < 3000; ++step) {
        c3.rotate(0.002, 0.002);
        auto afterRotate = c3.project(p, 1140, 733);
        auto m3 = c3.cameraToWorldMatrix4();
        c3.applyCameraToWorldMatrix4(m3);
        auto afterRoundtrip = c3.project(p, 1140, 733);
        double err = std::hypot(afterRoundtrip.x - afterRotate.x, afterRoundtrip.y - afterRotate.y);
        if (err > maxRoundtripError) maxRoundtripError = err;
        if (err > 1e-6) { fidelityOk = false; break; }
    }
    printf("polar-region roundtrip fidelity: %s (max error %.3e px over 3000 round-trips)\n",
           fidelityOk ? "OK" : "FAIL", maxRoundtripError);

    // DORDUNCU: orthonormalize drift — 5000 round-trip sonrasi matris hala
    // rotasyon mu (R*R^T = I, det=+1)?
    mm::Camera c4;
    c4.setView(mm::StandardView::Isometric);
    double maxOrthonormalError = 0.0;
    for (int i = 0; i < 5000; ++i) {
        c4.rotate(0.0017, -0.0009);
        auto mm4 = c4.cameraToWorldMatrix4();
        c4.applyCameraToWorldMatrix4(mm4);
        auto R = c4.cameraToWorldMatrix4(); // cameraToWorld rotasyonu
        // R^T * R ~ I kontrolu (m[0..10] rotasyon blogu)
        double e = 0.0;
        for (int r = 0; r < 3; ++r)
            for (int col = 0; col < 3; ++col) {
                double s = 0.0;
                for (int k = 0; k < 3; ++k)
                    s += R[static_cast<std::size_t>(k * 4 + r)] * R[static_cast<std::size_t>(k * 4 + col)];
                if (r == col) s -= 1.0;
                e = std::max(e, std::fabs(s));
            }
        maxOrthonormalError = std::max(maxOrthonormalError, e);
    }
    bool orthoOk = maxOrthonormalError < 1e-9;
    printf("orthonormality drift: %s (max error %.3e over 5000 round-trips)\n",
           orthoOk ? "OK" : "FAIL", maxOrthonormalError);

    return (ok && ok2 && fidelityOk && orthoOk) ? 0 : 1;
}
