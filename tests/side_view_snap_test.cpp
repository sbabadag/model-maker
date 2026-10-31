// YAN / ÖN GÖRÜNÜŞTE SNAP — iş düzlemi bakışa paralel kalırsa snap TAMAMEN ölür.
//
// Selahattin 2026-10-31: "view manipulator ile yan görünüşe geçtim, oradan snap
// almadı hiçbir yerden".
//
// Kök neden: SnapEngine::snap3D önce kameradan gelen ışını İŞ DÜZLEMİYLE
// kesiştirir (unprojectToPlane). Yatay bakışta (ön/yan görünüş) dünya XY
// düzlemi (normal Z) ışına PARALELdir → kesişim yok → erken `None`. Çözüm:
// ışına dik bir düzlem kullanmak (WorkPlane::fromViewDirection).
#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/grid.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near3(const Vec3& p, const Vec3& q, double tol = 1.0) {
    return std::fabs(p.x - q.x) <= tol && std::fabs(p.y - q.y) <= tol &&
           std::fabs(p.z - q.z) <= tol;
}

int main() {
    Document doc;
    // Düşey eleman (kolon): ekseni Z boyunca.
    doc.addModel(WireframeModel::line({0.0, 0.0, 0.0}, {0.0, 0.0, 3000.0}));

    SnapEngine engine;
    const int W = 1200, H = 800;
    const Vec3 corner{0.0, 0.0, 3000.0};

    // YAN GÖRÜNÜŞ: bakış yönü +X (dünya XY düzlemine PARALEL ışın).
    Camera side;
    side.setViewBasis({0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, 1500.0});
    const Vec3 viewDir = side.gridViewDirection();
    std::printf("       bakis=(%.2f %.2f %.2f)\n", viewDir.x, viewDir.y, viewDir.z);

    Vec2 s = side.project(corner, W, H);
    s.x += 4.0;
    s.y += 3.0;

    // (1) ESKİ/TUZAK HAL: dünya XY iş düzlemi ile -> snap YOK.
    {
        const SnapResult r = engine.snap3D(s, doc, side, W, H, 10.0, WorkPlane::world(),
                                           true, false, std::nullopt, nullptr, false);
        std::printf("       dunya XY duzlemi: tip=%d\n", (int)r.type);
        check(r.type == SnapType::None,
              "yan gorunus + dunya XY duzlemi: isin duzleme PARALEL -> snap YOK (tuzak)");
    }

    // (2) DÜZELTME: bakışa dik iş düzlemi -> uç noktasına snap.
    {
        const WorkPlane viewPlane = WorkPlane::fromViewDirection({0.0, 0.0, 0.0}, viewDir);
        const SnapResult r = engine.snap3D(s, doc, side, W, H, 10.0, viewPlane, true, false,
                                           std::nullopt, nullptr, false);
        std::printf("       bakisa dik duzlem: tip=%d nokta=(%.1f %.1f %.1f)\n", (int)r.type,
                    r.point.x, r.point.y, r.point.z);
        check(r.type == SnapType::Endpoint, "bakisa dik duzlem: yan gorunuste uca snap VAR");
        check(near3(r.point, corner), "snap noktasi kolon ucu (0,0,3000)");
    }

    // (3) ÖN GÖRÜNÜŞ (bakış +Y) aynı kural.
    {
        Camera front;
        front.setViewBasis({1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {0.0, 0.0, 1500.0});
        Vec2 fs = front.project(corner, W, H);
        fs.x += 3.0;
        const WorkPlane viewPlane =
            WorkPlane::fromViewDirection({0.0, 0.0, 0.0}, front.gridViewDirection());
        const SnapResult r = engine.snap3D(fs, doc, front, W, H, 10.0, viewPlane, true, false,
                                           std::nullopt, nullptr, false);
        check(r.type == SnapType::Endpoint && near3(r.point, corner),
              "on gorunuste de uca snap VAR (bakisa dik duzlem)");
    }

    std::printf("%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
