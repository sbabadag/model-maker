// KIRIS YUKU (BeamLoad) KARARLI KIMLIK (EntityId) ANAHTARI + yasam dongusu testi.
//
// OCC GEREKTIRMEZ; Linux'ta da kosar. Phase 2B: beamLoads_ (ve Application
// icindeki occShapes_) INDEKS anahtarindan KARARLI KIMLIK anahtarina gecti.
// Bu test, indeks-tabanli surumun SESSIZ hatasini KILITLER: bir varlik
// silinince/yeniden siralaninca kaydin BASKA bir varliga baglanmasi.
//
// Kilitlenen kurallar:
//   * Yuk, EntityId ile ayarlanir/okunur; indeks KALICI degildir.
//   * Arada baska varlik silinse de yuk DOGRU varlikta kalir.
//   * Yuklu varlik silinip GERI ALINIRSA (ayni kimlik) yuk yeniden baglanir.
//   * YENI varlik, silinmis bir varligin yukunu ASLA devralmaz (kimlik
//     yeniden kullanilmaz).
//   * clear() ve load() yukleri SIFIRLAR (onceki belgeden sizma yok).
//   * Ara varlik silinince KALAN varliklarin kimlikleri DEGISMEZ — occShapes_
//     id-anahtarinin dogrulugu bu invaryanta dayanir.
#include "model_maker/document.hpp"
#include "model_maker/entity_id.hpp"

#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    // 1) Kimlik anahtarli ayarla/oku; tekil.
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}); // id 1
        doc.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}); // id 2
        const EntityId a = doc.modelId(0);
        const EntityId b = doc.modelId(1);
        doc.setBeamLoad(a, {5.0, 0.0});
        const auto loadA = doc.getBeamLoad(a);
        check(loadA.has_value() && loadA->wY == 5.0, "kimlikle ayarlanan yuk kimlikle okunur");
        check(!doc.getBeamLoad(b).has_value(), "diger varligin yuku yok");
        check(doc.beamLoads().size() == 1, "yuk tablosu tek kayit");
        check(!doc.getBeamLoad(kInvalidEntityId).has_value(),
              "gecersiz kimlik -> kayit yok");
    }

    // 2) ARADA BASKA VARLIK SILININCE yuk DOGRU varlikta kalir.
    //    (Indeks anahtarli eski surumde silme indeksleri kaydirir ve kayit
    //     YANLIS varliga baglanirdi.)
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}); // A id 1
        doc.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}); // B id 2
        doc.addLine({0.0, 2.0, 0.0}, {1.0, 2.0, 0.0}); // C id 3
        const EntityId idA = doc.modelId(0);
        const EntityId idB = doc.modelId(1);
        doc.setBeamLoad(idB, {10.0, 0.0});
        doc.pushSnapshot();
        doc.deleteModels({0}); // A silinir -> B ve C birer indeks kayar
        check(doc.models().size() == 2, "silme sonrasi 2 model");
        check(doc.indexOfEntity(idB).has_value() && *doc.indexOfEntity(idB) == 0,
              "B artik indeks 0 (indeks KAYDI)");
        const auto loadB = doc.getBeamLoad(idB);
        check(loadB.has_value() && loadB->wY == 10.0, "yuk KIMLIKLE B'de kaldi (kaymadi)");
        check(!doc.getBeamLoad(idA).has_value(), "silinen A'nin yuku yok");
        // Indeks -> kimlik cozumu ayni yuku vermeli (cizim yolu bunu kullanir).
        const auto viaIndex = doc.getBeamLoad(doc.modelId(0));
        check(viaIndex.has_value() && viaIndex->wY == 10.0,
              "index->id cozumunde de dogru yuk (B)");
    }

    // 3) YUKLU VARLIK SILINIP GERI ALINIRSA: undo AYNI kimligi dondurur ->
    //    yuk YENIDEN BAGLANIR (silinen varligin kaydi korunur).
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}); // A
        doc.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}); // B
        const EntityId idB = doc.modelId(1);
        doc.setBeamLoad(idB, {7.0, 1.5});
        doc.pushSnapshot();
        doc.deleteModels({1});
        check(!doc.indexOfEntity(idB).has_value(), "B silindi (kimlik yok)");
        check(doc.canUndo(), "undo mumkun");
        check(doc.undo(), "undo basarili");
        const auto restored = doc.indexOfEntity(idB);
        check(restored.has_value(), "undo B'yi AYNI kimlikle geri getirdi");
        const auto loadB = doc.getBeamLoad(idB);
        check(loadB.has_value() && loadB->wY == 7.0 && loadB->wZ == 1.5,
              "yuk geri alinan varliga YENIDEN baglandi");
    }

    // 4) YENI varlik, silinmis varligin yukunu ASLA devralmaz.
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}); // A
        doc.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}); // B
        const EntityId idB = doc.modelId(1);
        doc.setBeamLoad(idB, {9.0, 0.0});
        doc.pushSnapshot();
        doc.deleteModels({1});
        doc.addLine({0.0, 5.0, 0.0}, {1.0, 5.0, 0.0}); // yeni varlik
        const EntityId newId = doc.modelId(doc.models().size() - 1);
        check(newId != idB, "yeni varlik YENI kimlik aldi");
        check(!doc.getBeamLoad(newId).has_value(), "yeni varlik yuku DEVRALMADI");
        check(doc.getBeamLoad(idB).has_value(),
              "silinen varligin yuku kendi kimliginde duruyor (yanlis varliga baglanmaz)");
    }

    // 5) YASAM DONGUSU: clear() yukleri temizler.
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        doc.setBeamLoad(doc.modelId(0), {1.0, 0.0});
        doc.clear();
        check(doc.beamLoads().empty(), "clear() kiris yuklerini temizler");
    }

    // 6) YASAM DONGUSU: load() yukleri temizler (onceki belgeden sizma yok).
    //    MMW formatini DEGISTIRMEZ — yukler zaten kalici degildir.
    {
        const auto tempPath = std::filesystem::temp_directory_path() /
                              "mm_beam_load_id_test.mmw";
        {
            Document source;
            source.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
            source.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});
            source.save(tempPath);
        }
        Document doc;
        doc.addLine({2.0, 0.0, 0.0}, {3.0, 0.0, 0.0});
        doc.setBeamLoad(doc.modelId(0), {4.0, 0.0});
        check(!doc.beamLoads().empty(), "load oncesi yuk var");
        doc.load(tempPath);
        check(doc.beamLoads().empty(),
              "load() onceki belgenin yuklerini temizler");
        check(doc.models().size() == 2, "load() model sayisi dosyadan geldi");
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
    }

    // 7) OCC tablosunun dayandigi invaryant: ara varlik silinince KALAN
    //    varliklarin KIMLIKLERI DEGISMEZ (occShapes_ id-anahtari dogru kalir).
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}); // A
        doc.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}); // B
        doc.addLine({0.0, 2.0, 0.0}, {1.0, 2.0, 0.0}); // C
        doc.addLine({0.0, 3.0, 0.0}, {1.0, 3.0, 0.0}); // D
        const EntityId idA = doc.modelId(0);
        const EntityId idC = doc.modelId(2);
        const EntityId idD = doc.modelId(3);
        doc.pushSnapshot();
        doc.deleteModels({1}); // ortadaki B silinir (en riskli kayma)
        check(doc.modelId(0) == idA && doc.modelId(1) == idC && doc.modelId(2) == idD,
              "ara silme sonrasi kalan kimlikler SABIT");
        std::set<EntityId> seen;
        for (const auto id : doc.modelIds()) seen.insert(id);
        check(seen.size() == doc.models().size(),
              "kimlikler tekil (silme sonrasi)");
    }

    // 8) Gecersiz kimlige yuk kaydedilmez (sentinel temiz kalir).
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        doc.setBeamLoad(kInvalidEntityId, {3.0, 0.0});
        check(doc.beamLoads().empty(), "gecersiz kimlige yuk KAYDEDILMEZ");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
