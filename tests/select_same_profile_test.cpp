// HIZLI SECIM ("ayni profilleri sec") — cekirdek testi (OCC GEREKTIRMEZ).
//
// Document::modelsWithSameProfile(index): verilen indeksteki modelin
// profilAdi (buyuk/kucuk harf DUYARSIZ) ile ayni profilAd'li TUM modellerin
// indekslerini dondurur. Kural:
//   * referans indeks gecersiz -> bos
//   * referans profilAdi BOS -> bos (profilsiz cizgi/nokta/3D face)
//   * sonuc artan sirada ve referans DAHIL
//   * buyuk/kucuk harf duyarsiz (HEB300 == heb300)
#include "model_maker/document.hpp"
#include "model_maker/geometry.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static void addModel(Document& doc, const char* profile) {
    WireframeModel m = WireframeModel::line(Vec3{0, 0, 0}, Vec3{1000, 0, 0});
    if (profile && *profile) {
        auto p = m.properties();
        p.profileName = profile;
        m.setProperties(std::move(p));
    }
    doc.addModel(std::move(m));
}

int main() {
    // --- 1) Gecersiz / profilsiz referans --------------------------------
    {
        Document doc;
        addModel(doc, "HEB300");
        check(doc.modelsWithSameProfile(999).empty(), "gecersiz indeks -> bos");
        check(doc.modelsWithSameProfile(0).size() == 1, "tek model: kendisi");
    }
    {
        Document doc;
        addModel(doc, nullptr);          // profilsiz cizgi
        addModel(doc, "HEB300");
        check(doc.modelsWithSameProfile(0).empty(), "profilsiz referans -> bos (menu yok)");
    }

    // --- 2) Ayni profili topla (referans dahil, artan sira) --------------
    {
        Document doc;
        addModel(doc, "IPE200");   // 0
        addModel(doc, "HEB300");   // 1
        addModel(doc, "IPE200");   // 2
        addModel(doc, "IPE200");   // 3
        addModel(doc, "HEB300");   // 4
        addModel(doc, nullptr);    // 5  profilsiz
        const auto r = doc.modelsWithSameProfile(2);
        check(r.size() == 3, "IPE200: 3 model (referans dahil)");
        check(r.size() == 3 && r[0] == 0 && r[1] == 2 && r[2] == 3,
              "IPE200: indeksler artan + referans dahil");
        const auto h = doc.modelsWithSameProfile(1);
        check(h.size() == 2 && h[0] == 1 && h[1] == 4, "HEB300: 2 model");
    }

    // --- 3) Buyuk/kucuk harf duyarsiz ------------------------------------
    {
        Document doc;
        addModel(doc, "HEB300");
        addModel(doc, "heb300");
        addModel(doc, "IPE200");
        const auto r = doc.modelsWithSameProfile(0);
        check(r.size() == 2 && r[0] == 0 && r[1] == 1,
              "HEB300 == heb300 (buyuk/kucuk harf duyarsiz)");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
