# MMW8 dosya codec'i (Phase 3.4B) + EntityId doygunluk kusuru

## Codec (mmw8_codec.hpp/.cpp, OCCT'siz)

- Veri modeli `Mmw8Document`: nextEntityId, metadata (BinTools format surumu + OCCT surumu),
  models (WireframeModel) + entityIds (birebir hizali), layers, grids, blobs, unknownBlocks.
- `writeMmw8(ostream, doc)` / `readMmw8(istream, doc)`. readMmw8 HATADA firlatir (sessiz
  wireframe-only YOK). Akislar std::ios::binary OLMALI (dosya I/O'su 3.4C'de).
- Format: "MMW8 <modelCount> <nextEntityId>\n" + L + model kayitlari (MMW7 + entityId 6. token)
  + G + kuyruk. Kuyruk etiket-gudumlu: "<TAG> <bodyLen>\n<bodyLen bayt>"; H (metadata, ZORUNLU,
  ilk) + B (blob'lar, en fazla bir) + bilinmeyen bloklar (skip + kayipsiz korunur).
- B blob kaydi: "<entityId> <blobLen> <sha256hex>\n<blobLen bayt>". SHA-256 butunlugu.
- Sinirlar: model 100k, layer/grid 1000, vertex 1M, edge 2M, face 1M, string 100k,
  blob 64 MiB (tekil), toplam blob 1 GiB. Ayirma ONCESI dogrulanir.
- Tam sayi ayristirma: parseUint64/parseInt64 tasma + negatif reddi ile (operator>> DEGIL —
  unsigned'a "-1" okumak sarmaya neden olurdu).

## SHA-256 (sha256.hpp/.cpp, kendine-yeterli)

- Repoda hazir SHA-256 YOKTU; OpenSSL/libcrypto mevcut ama Windows'a dis bagimlilik getirir.
  FIPS 180-4 uygulamasi yazildi. Vektorler: "", "abc", iki-blok, 1M 'a' (mmw8_codec_test).

## EntityId DOYGUNLUK KUSURU (rapor edildi, 3.4B'de duzeltilmedi)

`advanceEntityIdCounter` (entity_id.hpp) kMax'ta KELEPCELENIR (sarmaz). `allocateEntityId`
(document.cpp) kMax'a ulasinca AYNI id'yi tekrar tekrar doner -> YINELENEN EntityId (sessiz).
VersionId sayaci ise TUKENIR (0'a gecer) ve `set` reddeder — EntityId'de bu koruma YOK.
Pratikte erisilemez (2^64 id) ama "asla tekrar kullanilmaz" degismezligi sessizce bozulur.
DUZELTME (ayri faz): allocateEntityId tukenme durumunu reddetsin (throw veya invalid donmek),
advanceEntityIdCounter tukenme semantigi versin (VersionId gibi). Test plani:
1) advanceEntityIdCounter(kMax) tukenme isareti donmeli (kelepce DEGIL);
2) allocateEntityId tukendikten sonra tekrar ID URETMEMELI (throw/invalid);
3) coklu allocate sonrasi yinelenen ID yok. Harness Test 10 (VersionId tukenme) ornegi izlenir.
