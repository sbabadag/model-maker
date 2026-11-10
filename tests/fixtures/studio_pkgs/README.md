# Studio paketleri — uyum (golden) girdileri

Bu klasördeki paketler **Model Maker Studio'dan dışa aktarıldığı gibi** durur;
MM tarafında elle düzenlenmez (checksums.json bayt bayt doğrulanır — bir bayt
bile değişirse paket fail-closed reddedilir).

| Paket | Kaynak | Studio commit | Eklendiği iş |
|---|---|---|---|
| `mm_stiffened_end_plate.mmcpkg` | Studio S-8 kabul paketi (`C:\work\studio-kabul\20261110-1712-stiffened-end-plate`) | `9b50ac5` (`feature/ai-command-mvp`) | P-1/7 (golden + GT `t21`) |
| `mm_stiffened_base_plate.mmcpkg` | Studio S-6 kabul paketi (`C:\work\studio-kabul\20261110-1805-stiffened-base-plate`) | `9b50ac5` (`feature/ai-command-mvp`) | P-1/4 (pickCount=1 doğrulaması) |

## Denetleyen testler

- `model_maker_studio_package_golden_test` (P-1/7): her paket için yükleme +
  katalog metası + diyalog şeması; alın levhası için **kanonik girdiyle
  `planDigest` golden'ı** (`tests/golden/studio_end_plate.json`) ve
  "parametre değişince geometri değişir" kontrolü.
- GT `t21`: Studio alın levhasının uçtan uca GUI akışı (içe aktar → uygula →
  kolon boyu değiş → kaydet → aç).
- GT `t19` + `model_maker_component_package_dialog_test`: paket diyaloğu.
- GT `t18` + `model_maker_component_catalog_pick_test`: taban levhası
  `pickCount=1` ve katalog görünürlüğü.

## Bilinen sınırlar

- Paketlerde `catalog` bloğu YOK (Studio henüz yazmıyor). MM bu durumda
  `pickCount`'u recipe'deki üye referanslarından çıkarır ve paketi katalogda
  görünür kabul eder (`component_package.cpp`, P-1/4).
- `mm_stiffened_base_plate` **uygulanamıyor**: recipe `plate.polygon`
  kullanıyor, materyalizatör bu konturu katıya çeviremiyor
  ("levha kati uretilemedi"). Plan üretimi çalışıyor; engel P-2 kapsamında.
  Bu yüzden taban levhası için golden digest karşılaştırması YAPILMAZ.
- `mm_stiffened_end_plate` için iki açık bulgu STATE'te "P-1/7 BULGUSU"
  başlığıyla kayıtlı (süreklilik levhalarının ayna varyantları + kiriş profili
  değişiminde yeniden üretim).
- Golden'lar **test geçsin diye yeniden üretilmez**. Değiştirilecekse eski/yeni
  digest PR'da gösterilir ve STATE'e "Studio REPIN gerekli: <hash>" yazılır.
