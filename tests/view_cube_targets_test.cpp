// MODERN VIEWCUBE — 26 YON (6 yuz + 12 kenar + 8 kose) + GECIS ANIMASYONU.
// OCC GEREKTIRMEZ, platformdan bagimsiz (model_maker_core).
//
// KULLANICI SARTI: "kendi view cube umuzu kullanalim, modernlestirelim".
// Modern kup her yuz/kenar/koseye tiklanabilir olmali; yuzler Ctrl+1..7 ile
// AYNI yone gitmeli; kenar/kose yonleri komsu yuzlerle TUTARLI olmali; gecis
// animasyonlu olmali.
//
// Bu dosya GERCEK `ViewCube::layout` / `hitTest` / `viewCubeTargetRotation` /
// `viewCubeSlerpRotation` ve GERCEK `Camera`'yi cagirir — kopya mantik yok.
//
// EN GUCLU INVARYANT: her hedefin rotasyon matrisinin 3. SATIRI (= dunya
// bakis yonu) hedefin yon vektoru ile AYNIDIR. Yani "kenara tikladim" ->
// kamera gercekten o kenardan bakiyor.
#include "model_maker/camera.hpp"
#include "model_maker/view_cube.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

static Vec3 row2(const std::array<double, 9>& r) { return {r[6], r[7], r[8]}; }
static Vec3 row0(const std::array<double, 9>& r) { return {r[0], r[1], r[2]}; }
static Vec3 row1(const std::array<double, 9>& r) { return {r[3], r[4], r[5]}; }
static double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
static double det3(const std::array<double, 9>& r) {
    return r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
           r[2] * (r[3] * r[7] - r[4] * r[6]);
}
static bool orthonormal(const std::array<double, 9>& r, double eps = 1e-9) {
    return near(length(row0(r)), 1.0, eps) && near(length(row1(r)), 1.0, eps) &&
           near(length(row2(r)), 1.0, eps) && near(dot(row0(r), row1(r)), 0.0, eps) &&
           near(dot(row0(r), row2(r)), 0.0, eps) && near(dot(row1(r), row2(r)), 0.0, eps) &&
           near(det3(r), 1.0, eps);
}

// --- 26 hedefin tamami (yuz + kenar + kose) ---------------------------------
static std::vector<ViewCubeTarget> allTargets() {
    std::vector<ViewCubeTarget> targets;
    constexpr std::array<StandardView, 6> faces{
        StandardView::Left, StandardView::Right, StandardView::Bottom,
        StandardView::Top, StandardView::Back, StandardView::Front};
    for (const auto face : faces) {
        ViewCubeTarget t;
        t.kind = ViewCubeTargetKind::Face;
        t.face = face;
        targets.push_back(t);
    }
    for (const auto& direction : kViewCubeEdgeDirections) {
        ViewCubeTarget t;
        t.kind = ViewCubeTargetKind::Edge;
        t.dx = direction[0];
        t.dy = direction[1];
        t.dz = direction[2];
        targets.push_back(t);
    }
    for (int dx = -1; dx <= 1; dx += 2)
        for (int dy = -1; dy <= 1; dy += 2)
            for (int dz = -1; dz <= 1; dz += 2) {
                ViewCubeTarget t;
                t.kind = ViewCubeTargetKind::Corner;
                t.dx = dx;
                t.dy = dy;
                t.dz = dz;
                targets.push_back(t);
            }
    return targets;
}

int main() {
    std::printf("== MODERN VIEWCUBE: 26 yon + animasyon ==\n");

    // --- 1) Hedef sayilari ve yon tablosu -----------------------------------
    {
        const auto targets = allTargets();
        check(targets.size() == 26, "26 hedef: 6 yuz + 12 kenar + 8 kose");
        std::set<std::tuple<int, int, int>> directions;
        int edges = 0, corners = 0, faces = 0;
        for (const auto& t : targets) {
            const Vec3 d = viewCubeTargetDirection(t);
            directions.insert({static_cast<int>(std::lround(d.x * 1000)),
                               static_cast<int>(std::lround(d.y * 1000)),
                               static_cast<int>(std::lround(d.z * 1000))});
            if (t.kind == ViewCubeTargetKind::Edge) {
                ++edges;
                const int zeros = (t.dx == 0) + (t.dy == 0) + (t.dz == 0);
                check(zeros == 1, "kenar yonunde TAM BIR bilesen sifir");
            } else if (t.kind == ViewCubeTargetKind::Corner) {
                ++corners;
                check(t.dx != 0 && t.dy != 0 && t.dz != 0, "kose yonunde HICBIR bilesen sifir degil");
            } else {
                ++faces;
            }
        }
        check(edges == 12 && corners == 8 && faces == 6, "kenar/kose/yuz sayilari 12/8/6");
        check(directions.size() == 26, "26 yonun TAMAMI birbirinden farkli");
    }

    // --- 2) EN GUCLU INVARYANT: rotasyonun 3. satiri = hedef yonu ------------
    // (Kamera o ozellige bakiyor; yuz/kenar/kose fark etmez.)
    {
        int invariant = 0;
        for (const auto& t : allTargets()) {
            const auto r = viewCubeTargetRotation(t);
            const Vec3 expected = viewCubeTargetDirection(t);
            const Vec3 got = row2(r);
            if (near(got.x, expected.x) && near(got.y, expected.y) && near(got.z, expected.z))
                ++invariant;
            if (!orthonormal(r))
                check(false, "hedef rotasyonu ortonormal ve det=+1 DEGIL");
        }
        check(invariant == 26, "26 hedefin hepsi: rotasyonun 3. satiri = hedef yonu");
    }

    // --- 3) Yuzler Ctrl+1..7 ile AYNI; kenar/kose turntable ailesinden ------
    {
        bool facesMatch = true;
        for (const auto& t : allTargets()) {
            if (t.kind != ViewCubeTargetKind::Face) continue;
            const auto fromCube = viewCubeTargetRotation(t);
            const auto fromCamera = Camera::standardViewRotation(t.face);
            for (int i = 0; i < 9; ++i)
                if (!near(fromCube[static_cast<std::size_t>(i)], fromCamera[static_cast<std::size_t>(i)]))
                    facesMatch = false;
        }
        check(facesMatch, "yuz hedefi == Camera::standardViewRotation (Ctrl+1..7 ile AYNI)");

        bool edgesMatch = true;
        for (const auto& t : allTargets()) {
            if (t.kind == ViewCubeTargetKind::Face) continue;
            if (t.kind == ViewCubeTargetKind::Home) continue;
            double yaw = 0.0, pitch = 0.0;
            if (!viewCubeTurntableAngles(t, yaw, pitch)) { edgesMatch = false; continue; }
            const auto fromTurntable = Camera::turntableRotation(yaw, pitch);
            const auto fromCube = viewCubeTargetRotation(t);
            for (int i = 0; i < 9; ++i)
                if (!near(fromCube[static_cast<std::size_t>(i)], fromTurntable[static_cast<std::size_t>(i)]))
                    edgesMatch = false;
        }
        check(edgesMatch, "kenar/kose hedefi == turntable(yaw,pitch) — 6 yuzle ayni aile");
    }

    // --- 4) Kenar/kose acilari: komsu yuz normaliyle 45 / 54.7 derece -------
    {
        bool anglesOk = true;
        const auto faceNormal = [](StandardView v) -> Vec3 {
            switch (v) {
            case StandardView::Left: return {-1, 0, 0};
            case StandardView::Right: return {1, 0, 0};
            case StandardView::Bottom: return {0, 0, -1};
            case StandardView::Top: return {0, 0, 1};
            case StandardView::Back: return {0, -1, 0};
            default: return {0, 1, 0};
            }
        };
        for (const auto& t : allTargets()) {
            const Vec3 d = viewCubeTargetDirection(t);
            if (t.kind == ViewCubeTargetKind::Edge) {
                // Iki sifirdan farkli bilesen -> iki komsu normal; acilar 45.
                const double expected = 1.0 / std::sqrt(2.0);
                for (const auto face : {StandardView::Left, StandardView::Right, StandardView::Bottom,
                                        StandardView::Top, StandardView::Back, StandardView::Front}) {
                    const Vec3 n = faceNormal(face);
                    const double c = std::abs(dot(d, n));
                    if (c > 1e-9 && !near(c, expected, 1e-9)) anglesOk = false;
                }
            } else if (t.kind == ViewCubeTargetKind::Corner) {
                const double expected = 1.0 / std::sqrt(3.0);
                for (const auto face : {StandardView::Left, StandardView::Right, StandardView::Bottom,
                                        StandardView::Top, StandardView::Back, StandardView::Front}) {
                    const double c = std::abs(dot(d, faceNormal(face)));
                    if (c > 1e-9 && !near(c, expected, 1e-9)) anglesOk = false;
                }
            }
        }
        check(anglesOk, "kenarlar 45 derece, koseler 54.7 derece — komsu yuzlerle tutarli");
    }

    // --- 5) Animasyon: slerp ------------------------------------------------
    {
        const auto identity = Camera::standardViewRotation(StandardView::Top);
        const auto front = Camera::standardViewRotation(StandardView::Front);
        const auto at0 = viewCubeSlerpRotation(identity, front, 0.0);
        const auto at1 = viewCubeSlerpRotation(identity, front, 1.0);
        bool exactStart = true, exactEnd = true;
        for (int i = 0; i < 9; ++i) {
            if (!near(at0[static_cast<std::size_t>(i)], identity[static_cast<std::size_t>(i)])) exactStart = false;
            if (!near(at1[static_cast<std::size_t>(i)], front[static_cast<std::size_t>(i)])) exactEnd = false;
        }
        check(exactStart, "slerp(t=0) = baslangic rotasyonu");
        check(exactEnd, "slerp(t=1) = hedef rotasyon");

        bool smooth = true;
        double previousAngle = 0.0;
        for (int step = 0; step <= 10; ++step) {
            const double t = static_cast<double>(step) / 10.0;
            const auto r = viewCubeSlerpRotation(identity, front, t);
            if (!orthonormal(r, 1e-9)) smooth = false;
            // Kamera yonu (3. satir) Z'den Y'ye dogru tek yonlu donmeli.
            const Vec3 v = row2(r);
            const double angle = std::acos(std::clamp(-v.y, -1.0, 1.0)); // Z -> -Y arasi
            if (step > 0 && angle < previousAngle - 1e-9) smooth = false; // geri donus yok
            previousAngle = angle;
        }
        check(smooth, "slerp ara kareleri ortonormal ve tek yonlu (geri donus/kutup devrilmesi yok)");

        const auto same = viewCubeSlerpRotation(front, front, 0.5);
        bool identical = true;
        for (int i = 0; i < 9; ++i)
            if (!near(same[static_cast<std::size_t>(i)], front[static_cast<std::size_t>(i)], 1e-9))
                identical = false;
        check(identical, "slerp(R,R,0.5) = R (quaternion <-> matris gidis-donusu tutarli)");

        // GENEL rotasyon (ISO): quaternion'un DORT bileseni de sifirdan farkli,
        // yani isaret/terim hatalari burada gorunur olur (saf X/Y/Z
        // donuslerinde bazi terimler sifirdir ve hatayi maskeler).
        const auto iso = Camera::standardViewRotation(StandardView::Isometric);
        const auto sameIso = viewCubeSlerpRotation(iso, iso, 0.5);
        bool isoIdentical = true;
        for (int i = 0; i < 9; ++i)
            if (!near(sameIso[static_cast<std::size_t>(i)], iso[static_cast<std::size_t>(i)], 1e-9))
                isoIdentical = false;
        check(isoIdentical, "slerp(ISO,ISO,0.5) = ISO (genel rotasyonda gidis-donus)");
        // Ust -> ISO ara kareleri de gecerli rotasyon olmali.
        bool isoPath = true;
        for (int step = 1; step < 10; ++step) {
            const auto r = viewCubeSlerpRotation(identity, iso, static_cast<double>(step) / 10.0);
            if (!orthonormal(r, 1e-9)) isoPath = false;
        }
        check(isoPath, "Ust -> ISO slerp yolu boyunca gecerli rotasyon");

        // 180 derece (Ust -> Alt): kisa yol tanimli olmali, NaNsiz bitmeli.
        const auto bottom = Camera::standardViewRotation(StandardView::Bottom);
        const auto halfway = viewCubeSlerpRotation(identity, bottom, 0.5);
        check(orthonormal(halfway, 1e-9), "180 derece slerp gecerli rotasyon uretir (tekil durum yok)");
    }

    // --- 6) hitTest: yuz / kenar / kose / ISO / bos -------------------------
    {
        constexpr int width = 1200;
        Camera camera;
        // UST gorunusu: kup kareye projekte olur — 1 yuz, 4 kenar, 4 kose
        // gorunur ve konumlar kesin (62 px kenar, ortalar 31 px uzakta).
        camera.setView(StandardView::Top);
        const auto cube = ViewCube::layout(width, camera);

        int visibleEdges = 0, visibleCorners = 0, visibleFaces = 0;
        for (const bool v : cube.edgeVisible) if (v) ++visibleEdges;
        for (const bool v : cube.cornerVisible) if (v) ++visibleCorners;
        for (const auto& f : cube.faces) if (f.visible) ++visibleFaces;
        check(visibleEdges == 4 && visibleCorners == 4 && visibleFaces == 1,
              "UST gorunusu: 1 yuz + 4 kenar + 4 kose gorunur");

        // Yuz merkezi (karenin ortasi): hicbir kenar/kose yaricina girmez.
        for (const auto& face : cube.faces) {
            if (!face.visible) continue;
            int x = 0, y = 0;
            for (const auto& p : face.points) { x += p.x; y += p.y; }
            x /= 4; y /= 4;
            const auto hit = ViewCube::hitTest(x, y, width, camera);
            check(hit.has_value() && hit->kind == ViewCubeTargetKind::Face && hit->face == face.view,
                  "yuz merkezine tik: o yuz");
        }

        // Kenar orta noktalari: tam hedef yon.
        int edgeHits = 0;
        for (std::size_t i = 0; i < cube.edgeTargets.size(); ++i) {
            if (!cube.edgeVisible[i]) continue;
            const auto& mid = cube.edgeMidpoints[i];
            const auto hit = ViewCube::hitTest(mid.x, mid.y, width, camera);
            const auto& expected = cube.edgeTargets[i];
            if (hit && hit->kind == ViewCubeTargetKind::Edge && hit->dx == expected.dx &&
                hit->dy == expected.dy && hit->dz == expected.dz)
                ++edgeHits;
        }
        check(edgeHits == visibleEdges, "gorunur KENAR orta noktalarina tik: tam o kenar");

        // Kose noktalari: tam hedef yon. GIZLI kose ayni piksele projekte olsa
        // bile secilen GORUNUR kosedir.
        int cornerHits = 0;
        for (std::size_t i = 0; i < cube.cornerTargets.size(); ++i) {
            if (!cube.cornerVisible[i]) continue;
            const auto& point = cube.corners[i];
            const auto hit = ViewCube::hitTest(point.x, point.y, width, camera);
            const auto& expected = cube.cornerTargets[i];
            if (hit && hit->kind == ViewCubeTargetKind::Corner && hit->dx == expected.dx &&
                hit->dy == expected.dy && hit->dz == expected.dz)
                ++cornerHits;
        }
        check(cornerHits == visibleCorners, "gorunur KOSE noktalarina tik: tam o kose");
        // UST gorunuste gizli kose (or. alt-on-sag) usttekiyle AYNI piksele
        // duser; secim gorunur olana gitmeli (yukaridaki kontrol bunu kapsar).

        // ISO dugmesi ve panel disi.
        const int homeX = (cube.homeControl.left + cube.homeControl.right) / 2;
        const int homeY = (cube.homeControl.top + cube.homeControl.bottom) / 2;
        const auto home = ViewCube::hitTest(homeX, homeY, width, camera);
        check(home.has_value() && home->kind == ViewCubeTargetKind::Home, "ISO dugmesine tik: Home hedefi");
        check(!ViewCube::hitTest(600, 400, width, camera).has_value(), "kup disina tik: hedef yok");
    }

    // --- 7) ISO hedefi gercekten ISO'ya gider -------------------------------
    {
        ViewCubeTarget home;
        home.kind = ViewCubeTargetKind::Home;
        const auto r = viewCubeTargetRotation(home);
        const auto expected = Camera::standardViewRotation(StandardView::Isometric);
        bool same = true;
        for (int i = 0; i < 9; ++i)
            if (!near(r[static_cast<std::size_t>(i)], expected[static_cast<std::size_t>(i)])) same = false;
        check(same, "ISO hedefi standardViewRotation(Isometric) — roll korunur");
        // Ve geometrik olarak on-sag-ust koseden bakar.
        const Vec3 d = row2(r);
        check(d.x < 0.0 && d.y < 0.0 && d.z < 0.0, "ISO bakis yonu on-sag-ust kose (-,-,-)");
    }

    // --- 8) Panel/tiklanan bolge ayni dikdortgen ---------------------------
    {
        constexpr int width = 1200;
        Camera camera;
        const auto cube = ViewCube::layout(width, camera);
        check(ViewCube::containsWidget(cube.centerX, cube.centerY, width),
              "kup merkezi tiklanabilir panelin icinde");
        check(ViewCube::containsWidget(cube.homeControl.left + 2, cube.homeControl.top + 2, width),
              "ISO dugmesi panelin icinde (tiklanabilir)");
    }

    std::printf("\n%d kontrol, %d hata\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
