// ============================================================================
// A (veri butunlugu) — ROTASYON KANONIKLESTIRME GERI-YAZMA TESTI
//
// KULLANICI KURALI: profileRotation belgede HAM saklanir (kullanici 270 girerse
// belgede 270 kalir). Kanoniklestirme (cift-simetrik kesitte mod 180, dusey
// uyede refNormal) YALNIZ komponent girdisi ve digest icindir. Belgeye, panele
// ya da kayda kanonik deger GERI YAZILMAZ.
//
// Bu test su zinciri kanitlar:
//   Document (profileRotation=270) -> componentInputFromDocument
//   -> ComponentInput.main.profileRotation == 90 (KANONIK, cift-simetrik)
//   ama Document'taki model HALA 270 (HAM) — geri yazma YOK.
// Ayrica asimetrik kesitte 270 -> 270 (mod 360, ayna uye) dogrulanir.
// ============================================================================
#include "model_maker/component_input_builder.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/document.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

int main() {
    // 1) Dogrudan kanoniklestirme: cift-simetrik 270 -> 90, asimetrik 270 -> 270.
    {
        const MemberSectionSpec sym{300.0, 300.0, 11.0, 19.0, 0.0};    // centerOffset=0
        const MemberSectionSpec asym{300.0, 300.0, 11.0, 19.0, -150.0}; // Top anchor
        const MemberGeometry gSym = memberGeometryFromProfile(
            {0, 0, 0}, {0, 0, 3000}, /*connectionAtA=*/false,
            {1, 0, 0}, 270.0, sym);
        const MemberGeometry gAsym = memberGeometryFromProfile(
            {0, 0, 0}, {0, 0, 3000}, /*connectionAtA=*/false,
            {1, 0, 0}, 270.0, asym);
        CHECK(std::fabs(gSym.profileRotation - 90.0) < 1e-9,
              "cift-simetrik 270 -> 90 (kanonik)");
        CHECK(std::fabs(gAsym.profileRotation - 270.0) < 1e-9,
              "asimetrik 270 -> 270 (ayna uye, [0,360))");
    }

    // 2) GERI-YAZMA YOK: Document 270 saklar, girdi 90 gosterir.
    {
        Document doc;
        SteelProfile prof;
        prof.name = "HEA300";
        prof.height = 300; prof.width = 300;
        prof.plateThickness = 11; prof.flangeThickness = 19;

        WireframeModel m = WireframeModel::line({0, 0, 0}, {0, 0, 3000});
        EntityProperties p = m.properties();
        p.profileName = "HEA300";
        p.axisFromX = 0; p.axisFromY = 0; p.axisFromZ = 0;
        p.axisToX = 0; p.axisToY = 0; p.axisToZ = 3000;
        p.sectionNormalX = 1; p.sectionNormalY = 0; p.sectionNormalZ = 0;
        p.profileRotation = 270.0;
        p.sectionAnchor = SectionAnchor::Center;  // Center -> offset 0 -> cift-simetrik
        m.setProperties(std::move(p));
        doc.addModel(std::move(m));
        const EntityId id = doc.modelId(0);

        std::vector<SteelProfile> cat{prof};
        std::wstring err;
        auto in = componentInputFromDocument(doc, cat, id, {}, {}, &err);
        CHECK(in.has_value(), "componentInputFromDocument girdi kurar");
        if (in) {
            CHECK(std::fabs(in->main.profileRotation - 90.0) < 1e-9,
                  "girdi.main.profileRotation == 90 (kanonik)");
        }
        // Belgedeki HAM deger DEGISMEMELI.
        const double stored = doc.models()[0].properties().profileRotation;
        CHECK(std::fabs(stored - 270.0) < 1e-9,
              "belgede profileRotation hala 270 (geri yazma YOK)");
    }

    if (failures) {
        std::printf("%d kontrol basarisiz\n", failures);
        return 1;
    }
    std::printf("ROTASYON KANONIKLESTIRME GERI-YAZMA: TUM KONTROLLER GECTI\n");
    return 0;
}
