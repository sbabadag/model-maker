// KESIT REFERANS NORMALI — cekirdek testi (OCC GEREKTIRMEZ, Linux'ta da kosar).
//
// KULLANICI SARTI: "profil cizilirken profil ust basligi workplane ile paralel
// olacak."
//
//   1) profileSectionReferenceNormal: baslik plakalarinin paralel olacagi
//      duzlemin normali AKTIF IS DUZLEMINDEN gelir (kullanici UCS kurduysa).
//      ANCAK iki noktali gorunusun OTOMATIK gorunus duzlemi ise dunya Z doner;
//      yoksa elevasyonda cizilen her kirisin basligi bakisa paralel olur ve
//      kiris levha gibi "yatak" gorunurdu.
//   2) Referans normal KATIYLA SAKLANIR → MMW6 dosya round-trip.
//   3) ESKI dosyalar (MMW3/MMW5) varsayilan (0,0,1) ile AYNI davranisi verir
//      (geriye donuk uyumluluk; alan yoksa kati eskisi gibi yatay baslikli).
//   4) Donusumler referans normali de DONER: rotate/transform (undo ile geri),
//      polar dizi ve ayna — gumball yerel cercevesi katidan kaymaz.
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/view_definition.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
static bool nearV(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}
static Vec3 normalOf(const Document& doc, std::size_t index) {
    const auto& p = doc.models()[index].properties();
    return Vec3{p.sectionNormalX, p.sectionNormalY, p.sectionNormalZ};
}

// Profil kirisi ekle: iki uc + profil adi + referans normal.
static void addProfileLine(Document& doc, const Vec3& from, const Vec3& to,
                           const Vec3& sectionNormal) {
    WireframeModel model = WireframeModel::line(from, to);
    auto props = model.properties();
    props.profileName = "IPE200";
    props.axisFromX = from.x; props.axisFromY = from.y; props.axisFromZ = from.z;
    props.axisToX = to.x; props.axisToY = to.y; props.axisToZ = to.z;
    props.sectionNormalX = sectionNormal.x;
    props.sectionNormalY = sectionNormal.y;
    props.sectionNormalZ = sectionNormal.z;
    model.setProperties(std::move(props));
    doc.addModel(std::move(model));
}

int main() {
    // --- 1) Referans normal secimi -----------------------------------------
    {
        WorkPlane world{};
        world.normal = Vec3{0, 0, 1};
        check(nearV(profileSectionReferenceNormal(world, nullptr), Vec3{0, 0, 1}),
              "dunya is duzlemi → dunya Z");

        WorkPlane ucs{};
        ucs.normal = Vec3{0, 1, 0}; // XZ duzlemi (egik UCS)
        check(nearV(profileSectionReferenceNormal(ucs, nullptr), Vec3{0, 1, 0}),
              "egik UCS → UCS normali (baslik o duzleme paralel)");

        // Iki noktali gorunus: is duzlemi OTOMATIK gorunus duzlemidir.
        const auto view = viewFromTwoPoints(Vec3{0, 0, 0}, Vec3{6000, 0, 0});
        check(view.has_value(), "iki noktali gorunus uretildi");
        if (view) {
            const WorkPlane plane = viewWorkPlane(*view);
            check(nearV(profileSectionReferenceNormal(plane, &*view), Vec3{0, 0, 1}),
                  "gorunusun OTOMATIK duzlemi → dunya Z (elevasyon 'yatak' olmaz)");

            // Gorunus icinde FARKLI bir UCS kurulduysa o kullanilir.
            WorkPlane custom = plane;
            custom.normal = Vec3{0.6, 0.8, 0.0};
            check(nearV(profileSectionReferenceNormal(custom, &*view), Vec3{0.6, 0.8, 0}),
                  "gorunuste OZEL UCS → UCS normali");
        }

        WorkPlane broken{};
        broken.normal = Vec3{0, 0, 0};
        check(nearV(profileSectionReferenceNormal(broken, nullptr), Vec3{0, 0, 1}),
              "bozuk is duzlemi normali → dunya Z (cokme yok)");
    }

    // --- 2) MMW6 round-trip: referans normal saklanir -----------------------
    const std::filesystem::path file{"mm_section_normal_roundtrip.mmw"};
    {
        Document doc;
        addProfileLine(doc, Vec3{0, 0, 0}, Vec3{1000, 0, 0}, Vec3{0, 1, 0});
        addProfileLine(doc, Vec3{0, 0, 0}, Vec3{0, 1000, 0}, Vec3{0, 0, 1});
        doc.save(file);

        std::ifstream peek(file);
        std::string signature;
        peek >> signature;
        check(signature == "MMW6", "kayit MMW6 imzasi yazar");

        Document other;
        other.load(file);
        check(other.models().size() == 2, "MMW6: iki model yuklendi");
        check(other.models().size() == 2 &&
                  nearV(normalOf(other, 0), Vec3{0, 1, 0}, 1e-12) &&
                  nearV(normalOf(other, 1), Vec3{0, 0, 1}, 1e-12),
              "MMW6: kesit referans normali round-trip korunur");
        check(other.models().size() == 2 && other.models()[0].properties().profileName == "IPE200",
              "MMW6: profil adi da korunur");
    }

    // --- 3) Geriye donuk uyumluluk: MMW3 (alan YOK) ------------------------
    {
        const std::filesystem::path legacy{"mm_section_normal_legacy3.mmw"};
        std::ofstream out(legacy);
        // P blogu (MMW3): layer profil rot src fx fy fz tx ty tz linetype
        //   material color true? lw thick lts trans vis frozen lock plot desc
        out << "MMW3\n1\n"
            << "2 1 0 0 0\n"
            << "0 0 0\n100 0 0\n0 1\n"
            << "P 1:0 6:IPE200 0 -1 0 0 0 1000 0 0 7:BYLAYER 4:S235 256 0 0 -1 0 1 0 1 0 1 1 0:\n";
        out.close();

        Document other;
        other.load(legacy);
        check(other.models().size() == 1, "MMW3 (eski) dosya yuklendi");
        check(other.models().size() == 1 &&
                  nearV(normalOf(other, 0), Vec3{0, 0, 1}, 1e-12),
              "MMW3: alan yok → varsayilan (0,0,1), eski davranis AYNI");
        check(other.models().size() == 1 &&
                  other.models()[0].properties().profileName == "IPE200",
              "MMW3: profil ozellikleri yine okunur");
    }

    // --- 4) Donusumler referans normali de doner ---------------------------
    {
        Document doc;
        addProfileLine(doc, Vec3{0, 0, 0}, Vec3{1000, 0, 0}, Vec3{1, 0, 0});
        doc.pushSnapshot();
        doc.rotateModels({0}, Vec3{0, 0, 0}, Vec3{0, 0, 1}, 3.14159265358979323846 / 2.0);
        check(nearV(normalOf(doc, 0), Vec3{0, 1, 0}, 1e-9),
              "rotate 90° Z: referans normal de doner (gumball cercevesi kaymaz)");
        doc.undo();
        check(nearV(normalOf(doc, 0), Vec3{1, 0, 0}, 1e-9),
              "undo: referans normal geri gelir");
        doc.redo();
        check(nearV(normalOf(doc, 0), Vec3{0, 1, 0}, 1e-9), "redo: yeniden doner");
    }

    // Non-uniform olcek: duzlem normali (M^-1)^T ile dogru donusur.
    {
        Document doc;
        addProfileLine(doc, Vec3{0, 0, 0}, Vec3{1000, 0, 0}, Vec3{0, 1, 0});
        doc.pushSnapshot();
        // linear = diag(2,1,1) → duzlem normali (0,1,0) yonunde DEGISMEZ.
        const std::array<double, 9> linear{2, 0, 0, 0, 1, 0, 0, 0, 1};
        const std::array<double, 9> inverse{0.5, 0, 0, 0, 1, 0, 0, 0, 1};
        doc.transformModels({0}, Vec3{0, 0, 0}, linear, inverse);
        check(nearV(normalOf(doc, 0), Vec3{0, 1, 0}, 1e-9),
              "non-uniform olcek: eksene dik normal korunur");
    }

    // Polar dizi: kopya normalleri de doner.
    {
        WireframeModel source = WireframeModel::line(Vec3{1000, 0, 0}, Vec3{2000, 0, 0});
        auto props = source.properties();
        props.profileName = "IPE200";
        props.sectionNormalX = 1; props.sectionNormalY = 0; props.sectionNormalZ = 0;
        source.setProperties(std::move(props));
        const auto copies = polarArray2D(source, 4, Vec3{0, 0, 0});
        check(copies.size() == 3, "polar dizi 3 kopya uretir");
        if (copies.size() == 3) {
            const auto& p0 = copies[0].properties();
            check(nearV(Vec3{p0.sectionNormalX, p0.sectionNormalY, p0.sectionNormalZ},
                        Vec3{0, 1, 0}, 1e-9),
                  "polar dizi: kopya referans normali 90° doner");
        }
    }

    // Ayna (XY duzleminde +X ekseni etrafinda): normalin Y bileseni ters.
    {
        WireframeModel source = WireframeModel::line(Vec3{0, 0, 0}, Vec3{1000, 0, 0});
        auto props = source.properties();
        props.profileName = "IPE200";
        props.sectionNormalX = 0; props.sectionNormalY = 1; props.sectionNormalZ = 0;
        source.setProperties(std::move(props));
        const auto mirrored = mirrorModel2D(source, Vec3{0, 0, 0}, Vec3{1000, 0, 0});
        check(mirrored.has_value(), "ayna uretildi");
        if (mirrored) {
            const auto& p = mirrored->properties();
            check(nearV(Vec3{p.sectionNormalX, p.sectionNormalY, p.sectionNormalZ},
                        Vec3{0, -1, 0}, 1e-9),
                  "ayna: referans normal de yansitilir (Y ters)");
        }
    }

    std::filesystem::remove(file);
    std::filesystem::remove("mm_section_normal_legacy3.mmw");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
