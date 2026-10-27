# P2B — Paftada 2B düzenleme (MVP)

Durum: çalışan MVP. Plan: seçme, taşıma, 2B çizgi/daire/merkez çizgisi ekleme, geri al/yinele,
gizleme (zorunlu imalat ölçüsü/notu → INCOMPLETE), kalıcılık ve model yeniden üretiminde koruma.
Tam bir CAD editörü değildir; kapsam dışı bırakılanlar aşağıda listelenmiştir.

## Katmanlar

| Dosya | Rol |
|---|---|
| `include/model_maker/paper_editing.hpp`, `src/paper_editing.cpp` | Qt'siz çekirdek: hit-test, taşıma, şekil ekleme, görünürlük, INCOMPLETE değerlendirmesi, geri al/yinele, yeniden üretimde koruma |
| `include/model_maker/drawing.hpp` | `PaperShape` (kullanıcı 2B çizimi) + `visible` alanları (ölçü/not/mark) |
| `src/drawing_sheet_widget.cpp` | Fare ile düzenleme (seç/tasi/çiz), araç seçici, Geri al/Yinele/Gizle düğmeleri; ekran ve PDF aynı `paintSheet` |
| `tests/paper_editing_test.cpp` | 54 kontrol: hit-test, taşıma, değer değişmezliği, undo/redo, INCOMPLETE, kalıcılık, koruma |
| `tests/paper_editing_gui_test.cpp` | 22 kontrol: fare ile çizme/seçme/taşıma, düğmeler, ekran-PDF paritesi (300 dpi raster) |

## Sert kurallar

1. **Mühendislik verisi değişmez.** Taşıma yalnız yerleşim alanlarını yazar: görünüş `origin`,
   ölçü `offset/side/textT/textSide/textLevel`, mark `labelPos`, metin `pos`, şekil `a/b/center`.
   `Dimension2D::value/p1/p2` ve `measured()` her zaman modelden gelir (`measured == value` testi).
2. **Model dokunulmaz.** Düzenleme `DrawingSheet` üzerinde çalışır; `Document` revizyonu değişmez.
3. **Gizleme = INCOMPLETE.** `featureKey` dolu (imalat kaynaklı) bir ölçü veya zorunlu not
   (HoleCallout/PlateNote/StatusStamp/Legend) gizlenirse kontrol listesine `ANNOTATIONS` (Unresolved)
   eklenir, `fabrication.draft=true`, `unresolvedCodes += ANNOTATION_INCOMPLETE`. Çizim READY olmaz.
   Gizli öğeler bağımsız çakışma ölçümünde ve yerleşimde SAYILMAZ (yanlış NO_FIT üretilmez).
4. **Tek renderer.** `paintSheet` ekran, PNG ve PDF için aynı planı çizer; şekiller vektördür.
5. **Kalıcılık.** `PaperShape` + `visible` alanları `DrawingDocument` JSON'una yazılır (ayrı format yok).
   `serialize → deserialize → serialize` birebir aynıdır.

## Model yeniden üretiminde koruma (`preservePaperEdits`)

Eşleşme anahtarları (kaynak varsa korunur):

* görünüş: `kind | subject | detailPlate` → `origin`
* ölçü: görünüş anahtarı + `role | featureKey | chain | direction` → `offset/side/textT/textSide/textLevel/visible`
* mark: görünüş + `target | isAssembly` → `labelPos/visible`
* metin: görünüş + `kind | featureKey | ilk satır` → `pos/visible`
* kullanıcı şekilleri: **aynı `ElementId`** ile taşınır; `nextId` çakışmayacak şekilde büyütülür.

## Etkileşim (Qt)

* Araçlar: `Sec`, `Cizgi`, `Daire`, `Merkez cizgisi` (açılır liste).
* `Sec`: tıklama en yakın öğeyi seçer (öncelik: şekil > metin > mark > ölçü > görünüş; tolerans
  zoom'a göre kâğıt mm cinsinden ~6 px).
* Sol tuş sürükleme: seçili öğeyi taşır (ölçüde dışa doğru `offset` büyür, içe doğru 0.5 mm'de durur).
* `Delete`/`Backspace` veya **Gizle** düğmesi: seçiliyi gizler + INCOMPLETE değerlendirmesi.
* **Geri al / Yinele**: snapshot tabanlı, sınırlı geçmiş (64), değişiklik yoksa adım açılmaz.

## Kapsam dışı (bilinçli, MVP sınırı)

* Ölçü metnini serbest bırakıp kırma/taşıma (yalnız ölçü çizgisi uzaklığı + metin konumu).
* Görünüş gizleme/silme, ölçü silme (yalnız gizleme), yeni ölçü üretme, kesit görünüşü.
* Çoklu seçim, snap yakalama, hizalama kılavuzları, katmanlar, metin düzenleme.
* Pafta düzeni yeniden akıtma (autoLayout) sonrası elle yerleşimin otomatik yeniden çözümü;
  `preservePaperEdits` çağıranın (yeniden üretim akışının) sorumluluğundadır.

## Komutlar

```
cmake -S . -B build-hermes -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_DISABLE_FIND_PACKAGE_OpenCASCADE=ON
cmake --build build-hermes --target model_maker_paper_editing_test model_maker_paper_editing_gui_test
cd build-hermes && ctest -R "paper_editing" --output-on-failure
```
