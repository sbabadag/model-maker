// KESME NESNELERI SECIMI (P5) — cekirdek test.
//
// cutModifierPickGeometry + hitTestCutModifier2D/3D: kesme modifier'lerinin
// SECILEBILIR geometrisi ve isabet testi. Geometri renderer (drawCutSelectionHints)
// ile AYNI kaynaktan gelir (tek kaynak). OCC'siz, Linux'ta kosar.

#include "model_maker/camera.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/document.hpp"

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

int main() {
    // --- cutModifierPickGeometry ---
    {
        CutModifier m;
        m.kind = CutKind::PlaneCut;
        m.planeOrigin = {0, 0, 0};
        m.planeNormal = {0, 0, 1};
        const Bounds3 box{{-100, -100, -100}, {100, 100, 100}}; // extent=200 -> half=150
        const auto geo = cutModifierPickGeometry(m, box);
        check(!geo.isSegment, "PlaneCut kapi sekil (segment degil)");
        check(geo.polygon.size() == 4, "PlaneCut 4 kose");
        check(geo.polygon.size() == 4 && near3(geo.polygon[0], {-150, -150, 0}) &&
                  near3(geo.polygon[2], {150, 150, 0}),
              "PlaneCut kose koordinatlari (half=150)");
    }
    {
        CutModifier m;
        m.kind = CutKind::Fitting;
        m.planeOrigin = {10, 20, 30};
        m.planeNormal = {0, 0, 1};
        const Bounds3 box{{0, 0, 0}, {10, 10, 10}}; // extent=10 -> half=7.5
        const auto geo = cutModifierPickGeometry(m, box);
        check(geo.polygon.size() == 4 && near3(geo.polygon[0], {10 - 7.5, 20 - 7.5, 30}),
              "Fitting kose origin-kaydirilmis (half=7.5)");
    }
    {
        CutModifier m;
        m.kind = CutKind::PolygonCut;
        m.polygon = {{0, 0, 0}, {5, 0, 0}, {5, 5, 0}};
        const Bounds3 box{};
        const auto geo = cutModifierPickGeometry(m, box);
        check(!geo.isSegment && geo.polygon == m.polygon, "PolygonCut cokgeni birebir");
    }
    {
        CutModifier m;
        m.kind = CutKind::LineCut;
        m.polygon = {{0, 0, 0}, {10, 0, 0}, {99, 99, 99}}; // 3. nokta yok sayilir
        const Bounds3 box{};
        const auto geo = cutModifierPickGeometry(m, box);
        check(geo.isSegment && geo.polygon.size() == 2 && near3(geo.polygon[0], {0, 0, 0}) &&
                  near3(geo.polygon[1], {10, 0, 0}),
              "LineCut segment (2 nokta)");
    }
    {
        CutModifier m;
        m.kind = CutKind::BooleanCut;
        const Bounds3 box{};
        const auto geo = cutModifierPickGeometry(m, box);
        check(geo.polygon.empty(), "BooleanCut secim sekli yok (model olarak secilir)");
    }

    // --- hitTestCutModifier2D ---
    {
        Document doc;
        doc.addModel(WireframeModel::line({-100, -100, 0}, {100, 100, 0}));
        CutModifier m;
        m.kind = CutKind::PlaneCut;
        m.target = doc.modelId(0);
        m.planeOrigin = {0, 0, 0};
        m.planeNormal = {0, 0, 1};
        doc.addCutModifier(m);
        const auto inside = hitTestCutModifier2D({0, 0, 0}, doc, 10.0);
        check(inside && *inside == 0, "2D duzlem ici isabet");
        const auto outside = hitTestCutModifier2D({1000, 1000, 0}, doc, 10.0);
        check(!outside, "2D duzlem disi bos");
    }
    {
        Document doc;
        doc.addModel(WireframeModel::line({-100, -100, 0}, {100, 100, 0}));
        CutModifier m;
        m.kind = CutKind::LineCut;
        m.target = doc.modelId(0);
        m.polygon = {{-50, -50, 0}, {50, 50, 0}};
        m.planeNormal = {0, 0, 1};
        doc.addCutModifier(m);
        const auto on = hitTestCutModifier2D({0, 0, 0}, doc, 10.0);
        check(on && *on == 0, "2D cizgi uzeri isabet");
        const auto off = hitTestCutModifier2D({0, 50, 0}, doc, 10.0); // ~35px uzak
        check(!off, "2D cizgi disi bos");
    }

    // --- hitTestCutModifier3D (kamera ile) ---
    {
        Document doc;
        doc.addModel(WireframeModel::line({-100, -100, 0}, {100, 100, 0}));
        CutModifier m;
        m.kind = CutKind::PlaneCut;
        m.target = doc.modelId(0);
        m.planeOrigin = {0, 0, 0};
        m.planeNormal = {0, 0, 1};
        doc.addCutModifier(m);
        Camera cam;
        cam.reset();
        const int w = 800, h = 600;
        const Vec2 center = cam.project({0, 0, 0}, w, h);
        const auto hit = hitTestCutModifier3D(center, doc, cam, w, h, 10.0);
        check(hit && *hit == 0, "3D duzlem merkezi isabet");
        const Vec2 farAway = cam.project({5000, 5000, 0}, w, h);
        const auto miss = hitTestCutModifier3D(farAway, doc, cam, w, h, 10.0);
        check(!miss, "3D duzlem disi bos");
    }

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
