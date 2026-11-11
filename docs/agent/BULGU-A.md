# BULGU (a) — Studio alın levhası: `_b` süreklilik levhaları yanlış yerde

**Durum: TEŞHİS TAMAM, DÜZELTME İÇİN KARAR GEREKLİ.**
Tarih: 2026-11-11 · Kapsam: `mm_stiffened_end_plate` Studio paketi, Bulgu (a)

## 1. Şikâyet

Studio tarafında alın levhalı birleşimde `stiffener_top_b` / `stiffener_bot_b`
süreklilik levhaları, `_a` ikizlerinin aynası değil; ~155 mm kayık görünüyor.

## 2. Ölçüm (MM tarafı, gerçek koşum)

`tests/gt1/t24_bulgu_a_olcum.json` — HEB300 (0,0,0)→(0,0,3000) + IPE240
(0,0,2880)→(5000,0,2880), `appComponent mm_stiffened_end_plate`.

Üretilen katı AABB'leri (mm):

| rol | x | y | z |
|---|---|---|---|
| `end_plate` | 150 … 162 | −80 … 80 | 2620 … 2900 |
| `stiffener_bot_a` | 13.5 … 275.5 | −144.5 … 0 | 2759.9 … 2769.9 |
| `stiffener_bot_b` | 13.5 … 275.5 | −300 … −155.5 | 2759.9 … 2769.9 |
| `stiffener_top_a` | 13.5 … 275.5 | −144.5 … 0 | 2990.1 … 3000.1 |
| `stiffener_top_b` | 13.5 … 275.5 | −300 … −155.5 | 2990.1 … 3000.1 |

Gözlem:
- Kalınlık ekseni Z ✓ (yatay süreklilik levhası — doğru).
- İki levha (a/b) aralarında 11 mm (gövde) boşlukla **aynı tarafta** tiling
  yapıyor: y ∈ [−300, 0] — bu, kolonun `SectionAnchor::Top` yüzünden
  −300…0 aralığına oturan **kesit derinliği** ile birebir örtüşüyor.
- Kesitin **genişlik** ekseninde (x ±150) levha 262 mm'lik boyutuyla
  x ∈ [13.5, 275.5] aralığına düşüyor → **kolonun dışına taşıyor**.

## 3. Aynı recipe, kanonik (golden) girdi

`libs/mm_contract/tests/studio_package_golden_test.cpp` kanonik girdisi
(HEB300 + IPE240, kiriş kolon ekseninde y=0, `refNormal=(1,0,0)`):

| rol | x | y |
|---|---|---|
| `stiffener_top_a` | −131 … 131 | −150 … −5.5 |
| `stiffener_top_b` | −131 … 131 | 5.5 … 150 |

Yani kanonik girdide levhalar gövdenin **iki yanında**, kesitin içinde ve
simetrik; 262 mm'lik boyut **derinlik** ekseninde. Bu plan donmuş golden
digest'e (`178cb73b…d4818`) bağlı ve doğru.

## 4. Kök neden

`frame.member_face` (`src/recipe.cpp:751-830`) iki farklı eksen takımı üretiyor:

| durum | normal (w) | u | v |
|---|---|---|---|
| kiriş **flanşa** bakıyor (`av > au`) | ±`sf.height` | `sf.width` | eksen |
| kiriş **gövdeye** bakıyor (`au > av`) | ±`sf.width` | `sf.height` | eksen |

Recipe (`recipe.json`) ise `frame.offset`'te `du` ile levhaları **gövdeden
flanş kenarına** kaydırıyor ve `frame.rotate about u 90°` ile kalınlığı üye
eksenine alıyor. Bu, `u = genişlik` varsayımına dayanır:

- Flanş durumu (golden): `u = sf.width` → `du` genişlik boyunca, `h` boyutu
  derinlik boyunca, kalınlık eksen boyunca ✓
- Gövde durumu (uygulama yolu): `u = sf.height` → `du` derinlik boyunca,
  `h` boyutu genişlik boyunca ✗ (90° dönmüş; levha kesitin dışına taşıyor)

Uygulama yolunda kiriş gövde durumuna düşüyor, çünkü kolon
`sectionNormal = (0,1,0)` ile yerleştirilmiş: `sf.width` X, `sf.height` Y
oluyor ve +X yönünden gelen kiriş gövde yüzüne bakıyor.

Ek olarak uygulama yolu üye çerçevesini `sectionCenterOffset = −150`
(`SectionAnchor::Top`) ile veriyor; golden kanonik girdi merkez ankrajlı (0).
Bu, `_a`/`_b` çiftinin mutlak konumunu 150 mm kaydırıyor ama **simetri**
sorununun kaynağı değil (simetri `u` eksen takımından geliyor).

## 5. Neden düzeltme şimdi yapılmadı

- Studio'nun aynı model için ürettiği plan hâlâ **raporlanamıyor**
  (`planDigest` yok) → "Studio ile aynı" şartı REPIN'e kadar ölçülemez.
- İki aday düzeltme birbirinin tersi sonuç veriyor:
  1. **MM tarafı:** gövde yüzü durumunda da `u = sf.width` (yani `du`'yu
     normal boyunca uygula; `frame.offset` `du`'yu `w` ile çarp) — paket
     değişmez, mevcut golden digest risk altına girmez ama `frame.offset`
     semantiği tüm recipe'ler için değişir.
  2. **Paket tarafı:** Studio paketi gövde yüzü bağlantısını desteklemiyor
     kabul edilip fail-closed red verilmesi (sessiz yanlış geometri yerine).
- Hangisinin doğru olduğu Studio çıktısıyla netleşir; MM'yi tek başına
  değiştirmek kırıcı bir contract değişikliği olur (`frame.offset` semantiği).

## 6. Yeniden üretim

```
cd build-hermes
./model-maker.exe --test-script ../tests/gt1/t24_bulgu_a_olcum.json \
    --out "C:/work/mm-hermes-shots/<tarih>/t24"
```
Ölçüm için `MM_DUMP_FRAMES=1` (recipe çerçeve dökümü) ve
`src/component_materializer.cpp` içindeki `MAT-PLATE` tanı satırı
kullanıldı; **her ikisi de commit edilmedi** (geçici tanı).
