// VIEWCUBE — yuz hedefleri ve yerlesim tutarliligi (OCC'siz, Linux'ta kosar).
//
// Selahattin 2026-10-31: "view manipulator cok kotu, bos cizilen yuzleri var,
// world duzlemine gore dogru goruntulere gecmiyor".
//
// Bu test iki SOMUT invaryanti kilitler:
//  1) Her YUZ hedefi, o yuzu one getirir (hedef rotasyonun derinlik bileseni
//     +1). Yanlis eslesme = "kup yanlis goruntuye goturuyor".
//  2) ISO'da tam 3 yuz gorunur, ucunun de alani > 0 ve etiket merkezi panel
//     ICINDE. Alan 0 / merkez disi = "bos cizilen yuz".
#include "model_maker/camera.hpp"
#include "model_maker/view_cube.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static double area(const std::array<CubePoint, 4>& p) {
    double a = 0.0;
    for (int i = 0; i < 4; ++i) {
        const auto& u = p[i];
        const auto& v = p[(i + 1) % 4];
        a += static_cast<double>(u.x) * v.y - static_cast<double>(v.x) * u.y;
    }
    return std::fabs(a) * 0.5;
}

int main() {
    // layout() tanim sirasi: Left, Right, Bottom, Top, Back, Front
    struct Def { const char* name; StandardView view; Vec3 normal; };
    const Def defs[] = {
        {"Left", StandardView::Left, {-1, 0, 0}}, {"Right", StandardView::Right, {1, 0, 0}},
        {"Bottom", StandardView::Bottom, {0, 0, -1}}, {"Top", StandardView::Top, {0, 0, 1}},
        {"Back", StandardView::Back, {0, -1, 0}}, {"Front", StandardView::Front, {0, 1, 0}}};

    for (const auto& d : defs) {
        Camera cam;
        cam.setView(d.view);
        const double depth = cam.viewTransform(d.normal).z;
        std::printf("  %-7s hedef derinlik=%.3f\n", d.name, depth);
        check(depth > 0.99, "yuz hedefi KENDI yuzunu one getirir");
    }

    Camera top;
    top.setView(StandardView::Top);
    const ViewCubeLayout lt = ViewCube::layout(1400, top);
    int visibleTop = 0;
    for (const auto& f : lt.faces) visibleTop += f.visible ? 1 : 0;
    check(visibleTop == 1, "UST gorunusunde tam 1 yuz gorunur");

    Camera front;
    front.setView(StandardView::Front);
    const ViewCubeLayout lf = ViewCube::layout(1400, front);
    int visibleFront = 0;
    for (const auto& f : lf.faces) visibleFront += f.visible ? 1 : 0;
    check(visibleFront == 1, "ON gorunusunde tam 1 yuz gorunur");

    Camera iso;
    iso.setView(StandardView::Isometric);
    const ViewCubeLayout li = ViewCube::layout(1400, iso);
    const CubeRect panel = ViewCube::hostBounds(1400, 900);
    int visibleIso = 0;
    int labelled = 0;
    for (const auto& f : li.faces) {
        if (!f.visible) continue;
        ++visibleIso;
        const double a = area(f.points);
        double cx = 0, cy = 0;
        for (const auto& p : f.points) { cx += p.x; cy += p.y; }
        cx /= 4.0; cy /= 4.0;
        const bool inside = cx > panel.left && cx < panel.right && cy > panel.top && cy < panel.bottom;
        std::printf("  ISO yuz view=%d alan=%.1f merkez=(%.0f,%.0f) panelIcinde=%d\n", (int)f.view,
                    a, cx, cy, inside ? 1 : 0);
        check(a > 400.0, "ISO gorunur yuz alani > 400 px^2 (etiket cizilebilir)");
        check(inside, "ISO yuz etiket merkezi panel ICINDE");
        if (a > 30.0 && inside) ++labelled;
    }
    check(visibleIso == 3, "ISO'da tam 3 yuz gorunur");
    check(labelled == visibleIso, "ISO'da gorunen her yuz etiketlenebilir");

    std::printf("%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
