// PORTAL / EAVES — gerçek kullanıcı modeli regresyonu (Selahattin 2026-10-31).
//
// portal_1.mmw: HEA240 kolon (90° dönük, SectionAnchor Top) + IPE400 eğik
// mahya kirişi (Top ankraj). Diyalog önizlemesi ile ana modelin AYNI yeri
// göstermesi gerekir:
//   - kolon yüzü          x = 115  (kolon bbox [-115, 115])
//   - alın levhası        x = 115 … 135 (20 mm, gap = 0, kolon yüzüne oturur)
//   - kiriş kesim düzlemi x = 135  (levhanın arka yüzü — kiriş kolona GİRMEZ)
//   - levha üstü kolon tepesine kırpılır (z = 5000), üstüne taşmaz
// Bu değerler plan (önizleme) tarafında sabitlenir; materializer aynı planı
// kullandığı için model tarafı da aynı yerleşimi üretir.
#include "model_maker/component_engine.hpp"
#include <cmath>
#include <cstdio>

using namespace mm;

static int bad = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    bad += ok ? 0 : 1;
}
static bool near(double a, double b, double tol = 1.0) {
    return std::fabs(a - b) <= tol;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    ComponentInput in;
    // Kolon: HEA240, düşey, 90° dönük, eksen kesitin -x yüzünde (Top anchor).
    in.main.id = 45;
    in.main.axisFrom = {-115.0, 12000.0, 0.0};
    in.main.axisTo = {-115.0, 12000.0, 5000.0};
    in.main.refNormal = {0.0, 0.0, 1.0};
    in.main.secWidth = 240.0;
    in.main.secHeight = 230.0;
    in.main.secWebThickness = 7.5;
    in.main.secFlangeThickness = 12.0;
    in.main.profileRotation = 90.0;
    in.main.sectionCenterOffset = -115.0;   // Top anchor = -h/2

    // Kiriş: IPE400, 9.35° eğik, Top ankraj (eksen üst başlıkta).
    MemberGeometry beam;
    beam.id = 47;
    beam.axisFrom = {0.0, 12000.0, 5000.0};
    beam.axisTo = {5997.8, 12000.0, 5986.7};
    beam.refNormal = {0.0, 0.0, 1.0};
    beam.secWidth = 180.0;
    beam.secHeight = 400.0;
    beam.secWebThickness = 8.6;
    beam.secFlangeThickness = 13.5;
    beam.sectionCenterOffset = -200.0;      // Top anchor = -h/2
    in.secondary.push_back(beam);

    const GenerationPlan plan =
        ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
    check(plan.ok, "portal: plan uretildi");
    if (!plan.ok) {
        std::printf("       error=%ls\n", plan.error.c_str());
        return 1;
    }

    // Alın levhası
    std::size_t endPlate = plan.plates.size();
    for (std::size_t i = 0; i < plan.plateKeys.size(); ++i)
        if (plan.plateKeys[i] == "haunch_end_plate") endPlate = i;
    check(endPlate < plan.plates.size(), "portal: alin levhasi planda var");
    if (endPlate < plan.plates.size()) {
        const PlateSpec& p = plan.plates[endPlate];
        const double front = p.center.x - std::fabs(p.axis.x) * p.thickness * 0.5;
        const double back = p.center.x + std::fabs(p.axis.x) * p.thickness * 0.5;
        std::printf("       levha x[%.2f %.2f] t=%.1f h=%.1f\n", front, back,
                    p.thickness, p.height);
        check(near(front, 115.0), "portal: levha kolon yuzune oturur (x=115)");
        check(near(p.thickness, 20.0), "portal: levha kalinligi 20 mm");
        check(near(p.center.z + p.height * 0.5, 5000.0),
              "portal: levha ustu kolon tepesinde kalir (z=5000)");
        check(p.center.z - p.height * 0.5 < 5000.0 - 300.0,
              "portal: levha haunch derinligini asagi dogru kapsar");
    }

    // Kiriş kesimi
    check(plan.cuts.size() == 1, "portal: tek kiris kesimi uretilir");
    if (!plan.cuts.empty()) {
        const CutModifier& c = plan.cuts.front();
        std::printf("       kesim tgt=%llu o=(%.2f %.2f %.2f) n=(%.2f %.2f %.2f) keep=%d\n",
                    (unsigned long long)c.target, c.planeOrigin.x, c.planeOrigin.y,
                    c.planeOrigin.z, c.planeNormal.x, c.planeNormal.y, c.planeNormal.z,
                    c.keepPositive ? 1 : 0);
        check(c.target == beam.id, "portal: kesim kirişe uygulanir");
        check(c.kind == CutKind::PlaneCut, "portal: kesim duzlem kesimi");
        check(near(c.planeOrigin.x, 135.0),
              "portal: kiris levhanin arka yuzunde kesilir (x=135)");
        check(near(c.planeNormal.x, 1.0) && near(c.planeNormal.y, 0.0) &&
                  near(c.planeNormal.z, 0.0),
              "portal: kesim duzlemi DUSEY (kolon yuzune paralel)");
        check(c.keepPositive, "portal: kesim kirisin uzak tarafini korur");
        check(c.hidden, "portal: kesim component ic elemani (gizli)");
    }

    // Kirişin kesilen ucu kolonun DISINDA olmali: kesim duzlemi kolon
    // bbox'inin (x=115) en az levha kalinligi kadar disinda.
    const double columnFace = 115.0;
    if (!plan.cuts.empty())
        check(plan.cuts.front().planeOrigin.x > columnFace + 1.0,
              "portal: kiris kolonun icine girmez");

    std::printf(bad == 0 ? "ALL PASS\n" : "%d FAIL\n", bad);
    return bad == 0 ? 0 : 1;
}
