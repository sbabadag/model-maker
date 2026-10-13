// KARARLI VARLIK KIMLIGI (EntityId) cekirdek testi.
//
// OCC GEREKTIRMEZ; Linux'ta da kosar. Amac: Document'in varlik kimligi
// altyapisini KILITLEMEK — kimlikler ekleme/silme/degistirme/undo/redo
// boyunca DOGRU varlikla eslesir, tektir, monoton artar ve MMW1-7 yukleme
// deterministik/catismasiz kimlik uretir.
//
// Kilitlenen kurallar:
//   * Vektor indeksi KALICI kimlik DEGIL (silme sonrasi kimlikler kayar).
//   * Yeni varlik -> YENI kimlik; ayni varlik geri gelince AYNI kimlik.
//   * 1:1 replaceModel kimligi KORUR; 1:N yeni kimlikler uretir.
//   * kInvalidEntityId (0) hicbir varligin kimligi olamaz.
//   * modelIds_.size() == models().size() her mutasyondan sonra.
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

// Temel invaryant: kimlik vektoru modellerle hizali, hepsi gecerli ve TEKIL.
static bool invariantsHold(const Document& doc) {
    if (doc.modelIds().size() != doc.models().size()) return false;
    std::set<EntityId> seen;
    for (const auto id : doc.modelIds()) {
        if (id == kInvalidEntityId) return false;
        if (!seen.insert(id).second) return false;
    }
    return true;
}

static bool containsId(const Document& doc, EntityId id) {
    for (const auto existing : doc.modelIds())
        if (existing == id) return true;
    return false;
}

int main() {
    // 1) Taze belge: kimlikler deterministik (1'den) ve tekil.
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        doc.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});
        doc.addLine({0.0, 2.0, 0.0}, {1.0, 2.0, 0.0});
        check(doc.modelId(0) == 1 && doc.modelId(1) == 2 && doc.modelId(2) == 3,
              "taze belgede kimlikler sirayla 1,2,3");
        check(invariantsHold(doc), "temel invaryant: hizalama + tekillik");
        check(!doc.modelId(3) && doc.modelId(3) == kInvalidEntityId,
              "gecersiz indeks -> kInvalidEntityId");
        check(doc.indexOfEntity(2).has_value() && *doc.indexOfEntity(2) == 1,
              "indexOfEntity(2) -> 1");
        check(!doc.indexOfEntity(999).has_value(), "bilinmeyen kimlik -> nullopt");
        check(!doc.indexOfEntity(kInvalidEntityId).has_value(),
              "kInvalidEntityId -> nullopt (gecerli kimlik degil)");
    }

    // 2) Silme: kalan varliklar KIMLIKLERINI korur (indeks kayar, kimlik sabit).
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});   // id 1
        doc.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});   // id 2
        doc.addLine({0.0, 2.0, 0.0}, {1.0, 2.0, 0.0});   // id 3
        const EntityId id1 = doc.modelId(0), id2 = doc.modelId(1), id3 = doc.modelId(2);
        doc.pushSnapshot();
        doc.deleteModels({1});                            // id2 silinir
        check(doc.models().size() == 2, "silme sonrasi 2 model");
        check(doc.modelId(0) == id1 && doc.modelId(1) == id3,
              "silme sonrasi kalanlarin KIMLIKLERI korunur (kaymaz)");
        check(!containsId(doc, id2), "silinen kimlik artik yok");
        check(invariantsHold(doc), "silme sonrasi invaryant");

        // Undo: silinen varlik AYNI kimlikle ve AYNI indekse geri gelir.
        doc.undo();
        check(doc.models().size() == 3, "undo silmeyi geri alir");
        check(doc.modelId(1) == id2, "undo silinen varligi AYNI kimlikle geri getirir");
        check(doc.modelId(0) == id1 && doc.modelId(2) == id3,
              "undo digerlerinin kimligini bozmaz");
        check(invariantsHold(doc), "undo silme sonrasi invaryant");

        // Redo: tekrar silinir.
        doc.redo();
        check(doc.models().size() == 2 && !containsId(doc, id2),
              "redo silmeyi yeniden uygular (kimlik gider)");
        check(invariantsHold(doc), "redo silme sonrasi invaryant");
    }

    // 3) Ekleme undo/redo: ayni varlik AYNI kimlikle geri gelir.
    {
        Document doc;
        doc.pushSnapshot();
        doc.addLine({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0});   // id 1, op kaydedilir
        const EntityId added = doc.modelId(0);
        check(added != kInvalidEntityId, "eklenen varligin gecerli kimligi var");
        doc.undo();
        check(doc.models().empty() && doc.modelIds().empty(),
              "undo eklemeyi kaldirir (model + kimlik)");
        doc.redo();
        check(doc.models().size() == 1 && doc.modelId(0) == added,
              "redo ayni varligi AYNI kimlikle geri koyar");
        check(invariantsHold(doc), "ekle undo/redo sonrasi invaryant");
    }

    // 4) replaceModel 1:1 -> kimlik KORUNUR (kati yeniden uretim ayni varliktir).
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});   // id 1
        const EntityId before = doc.modelId(0);
        doc.pushSnapshot();
        doc.replaceModel(0, {WireframeModel::line({0.0, 0.0, 0.0}, {2.0, 0.0, 0.0})});
        check(doc.models().size() == 1 && doc.modelId(0) == before,
              "1:1 replaceModel kimligi KORUR");
        doc.undo();
        check(doc.modelId(0) == before, "1:1 replace undo kimligi korur");
        doc.redo();
        check(doc.modelId(0) == before, "1:1 replace redo kimligi korur");
        check(invariantsHold(doc), "1:1 replace sonrasi invaryant");
    }

    // 5) replaceModel 1:N -> YENI kimlikler; eski kimlik emekliye ayrilir.
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {3.0, 0.0, 0.0});   // id 1
        const EntityId oldId = doc.modelId(0);
        doc.pushSnapshot();
        doc.replaceModel(0, {WireframeModel::line({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}),
                             WireframeModel::line({1.0, 0.0, 0.0}, {3.0, 0.0, 0.0})});
        check(doc.models().size() == 2, "1:N replace iki varlik uretir");
        check(!containsId(doc, oldId), "1:N replace eski kimligi emekliye ayirir");
        check(doc.modelId(0) != doc.modelId(1) &&
                  doc.modelId(0) != kInvalidEntityId && doc.modelId(1) != kInvalidEntityId,
              "1:N replace yeni TEKIL kimlikler uretir");
        const EntityId n0 = doc.modelId(0), n1 = doc.modelId(1);
        check(invariantsHold(doc), "1:N replace sonrasi invaryant");

        doc.undo();
        check(doc.models().size() == 1 && doc.modelId(0) == oldId,
              "1:N replace undo eski varligi AYNI kimlikle geri getirir");
        check(invariantsHold(doc), "1:N replace undo sonrasi invaryant");
        doc.redo();
        check(doc.models().size() == 2 && doc.modelId(0) == n0 && doc.modelId(1) == n1,
              "1:N replace redo yeni kimlikleri AYNI sekilde geri getirir");
        check(invariantsHold(doc), "1:N replace redo sonrasi invaryant");
    }

    // 6) Kopyalama: kopyalar YENI kimlik alir, orijinal kimligini korur.
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});   // id 1
        const EntityId original = doc.modelId(0);
        doc.pushSnapshot();
        doc.copyModels({0}, {10.0, 0.0, 0.0});
        check(doc.models().size() == 2, "kopya eklendi");
        check(doc.modelId(0) == original,
              "kopyalama orijinalin kimligini DEGISTIRMEZ");
        check(doc.modelId(1) != original && doc.modelId(1) != kInvalidEntityId,
              "kopya YENI bir kimlik alir");
        const EntityId copyId = doc.modelId(1);
        check(invariantsHold(doc), "kopyalama sonrasi invaryant");

        doc.undo();
        check(doc.models().size() == 1 && !containsId(doc, copyId),
              "undo kopyayi (ve kimligini) kaldirir");
        doc.redo();
        check(doc.models().size() == 2 && doc.modelId(1) == copyId,
              "redo kopyayi AYNI kimlikle geri koyar");
        check(invariantsHold(doc), "kopyalama undo/redo sonrasi invaryant");
    }

    // 7) MMW kaydet/yukle: taze belgede kimlikler deterministik 1..N, tekil.
    {
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "mm_entity_id_roundtrip.mmw";
        {
            Document doc;
            doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
            doc.addModel(WireframeModel::circle({5.0, 5.0, 0.0}, 2.0, 16));
            doc.addModel(WireframeModel::point({-4.0, 7.0, 2.0}));
            doc.save(path);
        }
        Document loaded;
        loaded.load(path);
        check(loaded.models().size() == 3, "yukleme 3 model getirir");
        check(loaded.modelId(0) == 1 && loaded.modelId(1) == 2 && loaded.modelId(2) == 3,
              "taze belgede yukleme kimlikleri deterministik 1..3");
        check(invariantsHold(loaded), "yukleme sonrasi invaryant");

        // Yukleme sonrasi eklenen varlik TASMALI (catismasiz) yeni kimlik alir.
        loaded.addLine({0.0, 0.0, 0.0}, {9.0, 9.0, 0.0});
        check(loaded.modelId(3) > 3 && loaded.modelId(3) != kInvalidEntityId,
              "yukleme sonrasi eklenen varlik catismasiz yeni kimlik alir");
        check(invariantsHold(loaded), "yukleme + ekleme sonrasi invaryant");

        // Mevcut belgeye yukleme: sayac MONOTON kalir (eski kimliklerle catisma yok).
        Document warm;
        warm.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});   // id 1
        warm.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});   // id 2, sayac 3
        warm.load(path);
        check(warm.models().size() == 3 && warm.modelId(0) == 3,
              "mevcut belgeye yuklemede sayac monotondur (3..)");
        check(invariantsHold(warm), "mevcut belgeye yukleme sonrasi invaryant");
    }

    // 8) clear() = yeni belge: sayac sifirlanir, kimlikler 1'den baslar.
    {
        Document doc;
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        doc.addLine({0.0, 1.0, 0.0}, {1.0, 1.0, 0.0});
        doc.clear();
        check(doc.models().empty() && doc.modelIds().empty(),
              "clear model ve kimlikleri bosaltir");
        doc.addLine({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        check(doc.modelId(0) == 1, "clear sonrasi kimlikler 1'den yeniden baslar");
        check(invariantsHold(doc), "clear sonrasi invaryant");
    }

    // 9) Sayaç TUKENME semantigi: gecerli kimlikler 1..(UINT64_MAX-1); UINT64_MAX
    //    tükenme isaretidir. Sayac ayni id'yi ASLA ikinci kez URETMEZ (eski
    //    kelepceleme YINELENEN kimlik olusturuyordu — Phase 3.4B duzeltmesi).
    {
        constexpr EntityId kExhausted = std::numeric_limits<EntityId>::max();
        check(kInvalidEntityId == 0, "kInvalidEntityId = 0 (gecersiz sentinel)");
        check(kExhaustedEntityId == kExhausted, "kExhaustedEntityId = UINT64_MAX");
        check(advanceEntityIdCounter(0) == 1, "advance(0) -> 1");
        check(advanceEntityIdCounter(1) == 2, "advance(1) -> 2");
        check(advanceEntityIdCounter(kExhausted - 2) == kExhausted - 1,
              "advance(max-2) -> max-1 (SON gecerli id)");
        check(advanceEntityIdCounter(kExhausted - 1) == kExhausted,
              "advance(max-1) -> max (TUKENDI isareti)");
        check(advanceEntityIdCounter(kExhausted) == kExhausted,
              "advance(max) tukendi, kalir (ayni id TEKRAR uretilmez)");
        check(advanceEntityIdCounter(kExhausted) != kInvalidEntityId,
              "tukenme 0'a SARMAZ (gecersiz sentinel uretilmez)");
        check(kExhaustedEntityId != 1 && kExhaustedEntityId != kInvalidEntityId,
              "UINT64_MAX gecerli bir varlik kimligi DEGILDIR");
    }

    // 10) SURUM kimligi sayaci: TUKENME semantigi — ayni id ASLA TEKRAR
    //     URETILMEZ. (VersionId'de kelepceleme YANLIS geometri dondururdu:
    //     ayni slot'ta eslesen ILK surum kazanir. Bu yuzden sayac kMax'tan
    //     sonra kInvalidVersionId'ye gecer ve magaza reddeder.)
    {
        constexpr VersionId kMax = std::numeric_limits<VersionId>::max();
        check(kInvalidVersionId == 0, "kInvalidVersionId = 0 (sentinel)");
        check(advanceVersionIdCounter(0) == 0, "advance(0) tukendi, kalir (0)");
        check(advanceVersionIdCounter(1) == 2, "advance(1) -> 2");
        check(advanceVersionIdCounter(kMax - 1) == kMax, "advance(max-1) -> max");
        check(advanceVersionIdCounter(kMax) != kMax,
              "advance(max) max'ta KALMAZ (ayni id tekrar uretilmez)");
        check(advanceVersionIdCounter(kMax) == kInvalidVersionId,
              "advance(max) -> 0 (TUKENDI isareti)");
        check(advanceVersionIdCounter(advanceVersionIdCounter(kMax)) == kInvalidVersionId,
              "tukendikten sonra kalici olarak 0");
        // Uretilen id hicbir zaman gecersiz sentinel DEGILDIR.
        check(kInvalidVersionId != 1 && kInvalidVersionId != kMax,
              "0 gecerli bir id degildir");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
