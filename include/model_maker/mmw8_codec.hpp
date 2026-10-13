#pragma once
// MMW8 DOSYA KODEC'I (OCCT'siz, portatif cekirdek).
//
// MMW8 = MMW7-compatible metin cekirdegi + EntityId kaliciligi + binary B-Rep
// blob'lari (uzunluk-oncelikli, SHA-256 butunluklu) + ileriye-uyumlu
// etiket-gudumlu kuyruk. Bu codec Document::save/load'dan BAGIMSIZ bir
// serilestirme/ayristirma katmanidir; Phase 3.4C'de Document entegrasyonu icin
// temel olacaktir.
//
// OCC'sizdir: binary B-Rep blob'larini OCCT kullanmadan AYRIŞTIRIR ve OPAK
// olarak korur (içerigini yorumlamaz). SHA-256 (bkz. sha256.hpp) blob butunlugu
// icin kullanilir.
//
// HATA POLITIKASI: `readMmw8` her dogrulama hatasinda std::runtime_error
// FIRLATIR — sessiz wireframe-only gecis YOK. `writeMmw8` de ayni sekilde
// (ornek: bir blob'un entityId'si model listesinde yoksa) firlatir.

#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/grid.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

// --- kaynak tuketimi sinirlari (ayirma oncesi dogrulanir) ------------------
inline constexpr std::size_t kMmw8MaxModels = 100'000;
inline constexpr std::size_t kMmw8MaxLayers = 1'000;
inline constexpr std::size_t kMmw8MaxGrids = 1'000;
inline constexpr std::size_t kMmw8MaxVertices = 1'000'000;
inline constexpr std::size_t kMmw8MaxEdges = 2'000'000;
inline constexpr std::size_t kMmw8MaxFaces = 1'000'000;
inline constexpr std::size_t kMmw8MaxStringBytes = 100'000;
// Tekil B-Rep blob siniri (bayt).
inline constexpr std::size_t kMmw8MaxBlobSize = 64ull * 1024ull * 1024ull;
// Tum blob'larin TOPLAM byte siniri (tekilden ayri).
inline constexpr std::size_t kMmw8MaxTotalBlobBytes = 1ull << 30;  // 1 GiB
// Desteklenen BinTools format surumu araligi (blob kodlamasi icin). 0 = "OCC
// yok / wireframe-only" (OCC-kapali derlemenin yazdigi H blogu).
inline constexpr int kMmw8SupportedBinToolsVersionMin = 0;
inline constexpr int kMmw8SupportedBinToolsVersionMax = 4;  // VERSION_CURRENT

// MMW8 metadata (H blogu).
struct Mmw8Metadata {
    int binToolsFormatVersion{kMmw8SupportedBinToolsVersionMax};
    std::string occtVersion;  // or. "7.9.2"
};

// Bir B-Rep blob'u (opak baytlar), EntityId anahtarli.
struct Mmw8Blob {
    EntityId entityId{kInvalidEntityId};
    std::string bytes;    // ham BinTools binary B-Rep
    std::string sha256;   // 64 kucuk-hex karakter (okurken dogrulanir)
};

// Bilinmeyen kuyruk blogu (ileriye uyumluluk) — kayipsiz korunur.
struct Mmw8UnknownBlock {
    char tag{'\0'};
    std::string body;  // ham baytlar
};

// MMW8 belge icerigi (OCCT'siz veri modeli).
struct Mmw8Document {
    EntityId nextEntityId{1};
    Mmw8Metadata metadata;
    // models ile BIREBIR hizali EntityId listesi (models.size() == entityIds.size()).
    std::vector<WireframeModel> models;
    std::vector<EntityId> entityIds;
    std::unordered_map<std::string, EntityProperties> layers;
    std::vector<GridDefinition> grids;
    std::vector<Mmw8Blob> blobs;
    std::vector<Mmw8UnknownBlock> unknownBlocks;
};

// MMW8 belgeyi ikili akisa yazar. SHA-256 her blob icin HESAPLANIR ve yazilir.
// Bilinmeyen bloklar (doc.unknownBlocks) kayipsiz yeniden yazilir.
// Hata durumunda std::runtime_error firlatir.
void writeMmw8(std::ostream& out, const Mmw8Document& doc);

// MMW8 belgeyi ikili akistan okur ve dogrular. Hata durumunda (kisa dosya,
// tasma, negatif sayi, tekrar eden EntityId, yanlis SHA-256, tekrarlanan H/B,
// yanlis sira, boyut siniri asimi, bilinmeyen zorunlu surum, ...) firlatir.
void readMmw8(std::istream& in, Mmw8Document& doc);

} // namespace mm
