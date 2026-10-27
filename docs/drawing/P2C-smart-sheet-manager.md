# P2C — Akıllı Pafta Yöneticisi (Smart Sheet Manager)

Durum: çekirdek (OCCT'siz, Qt'siz, Linux ctest) + Qt önizleme / çok sayfalı vektör PDF.
Ayrı bir çizim modeli **yoktur**: çıktı yine `DrawingDocument` / `DrawingSheet` (P1/P2A şeması),
üst bilgi `DrawingDocument::setInfo()` ve `DrawingDocument::preferences()` içindedir.

## Varsayılanlar (kabul edilen ürün gereksinimi, JSON'da kalıcı)

| Tercih | Varsayılan | Anlam |
|---|---|---|
| `sheetSize` | `auto` | `autoFirst` (A3) → `autoMax` (A0) arası yatay A-serisi denenir |
| `drawingMode` | `single` | Öncelik: eksiksiz, okunaklı **tek pafta** |
| `autoMultiSheetFallback` | `true` | Tek pafta kaliteyi sağlamıyorsa bağlı çoklu pafta üret |

`DrawingPreferences{y}` tek kaynaktır; `autoSheetSize=false` ise yalnız `sheetSize` kullanılır
(sınır aşılmaz). `autoFirst`/`autoMax` yapılandırılabilir.

## Karar sırası (`buildDrawingSet`)

1. `buildSheet` (P2A) ile tüm görünüş/ölçü/poz/notlar üretilir; **imalat ölçüsü üretimi değişmez**.
2. Tek pafta kalite kapısı: `layout.ok && measureOverlaps(...).total()==0`.
   Yerleşim motoru `autoFirst..autoMax` kâğıtlarını ve standart ölçekleri dener (A3 → A2 → A1 → A0).
3. Tek pafta geçerliyse: `role=primary`, `sheetNumber=antet numarası`, `index` boş; `READY`
   yalnız imalat verisi de TAM ise (`!fabrication.draft`).
4. Değilse ve yedek **kapalı**: `ok=false`, `layoutValid=false`, `ready=false`, `issues` içinde
   `SINGLE_SHEET_NOT_VALID` + `MULTI_SHEET_DISABLED`; tek pafta `LAYOUT` maddesi `Unresolved`.
5. Yedek **açık**: `splitSheet` ile paftalara bölünür:
   * ana pafta: ana görünüşler (Front/Top/Side). Sığmazsa Side, sonra Top **kendi devam paftasına**
     taşınır (Front ana paftada kalır).
   * her delikli levha detayı **kendi detay paftasında** (deterministik; deneme-yanılma yok).
   * yazı boyutu küçültülmez, ölçü atılmaz; sığmayan pafta `NO_FIT`/`DIMENSION_UNPLACEABLE`
     olarak **açıkça** raporlanır (`SHEET <no>: <kod>`), set `READY` olmaz.

## Set bütünlüğü

* `DrawingSetInfo{id, primarySheetId, multiSheet, layoutValid, ready, issues, index}` — kalıcı set
  kimliği `DS-<antet no>-R<model revizyonu>` (deterministik).
* Her pafta: `role` (`primary` / `continuation` / `detail`), `parentSheetId`, `sheetIndex`,
  `sheetNumber` (`A100`, `A100-01`, …), `references` (hedef pafta + hedef görünüş + etiket).
* Numaralandırma/antet: bağlı paftalar ana paftanın numarasından türetilir; proje, revizyon ve
  revizyon tablosu ortaktır; başlık `"<ana başlık> - <pafta konusu>"`.
* Ana paftada **PAFTA LİSTESİ** notu (bağlı paftalar), her bağlı paftada **ANA PAFTA <no>** notu.
* Referanslar **çift yönlüdür** (ana → bağlı, bağlı → ana); `validateDrawingSet` hedef pafta,
  hedef görünüş, numara, indeks, revizyon ve yerleşim tutarlılığını doğrular.

## Kalıcılık (JSON v3)

* `preferences` bloğu (`sheetSize: auto|A4..A0`, `drawingMode`, `autoMultiSheetFallback`,
  `autoFirst`, `autoMax`), `drawingSet` bloğu (id, primarySheetId, index, issues, durum).
* v1/v2 kayıtlar okunur (varsayılan tercihler = auto/single/true, pafta = primary).
* `serialize → deserialize → serialize` birebir aynıdır (deterministik).

## PDF / GUI

* `exportDrawingSetPdf(document, path)`: her pafta **kendi kâğıt boyutunda** bir sayfa, vektör
  (gömülü raster yok). Sayfa boyutu `newPage()` öncesi ayarlanır (Qt sırası önemli).
* `DrawingPreviewDialog` set modu (`SetBuilder`): "Otomatik kâğıt" + "Sığmazsa bağlı paftalar"
  onay kutuları, pafta seçici, tek dosyada tüm paftaları PDF'e aktarma, JSON kaydetme.
* Eski tek-pafta `Builder` yolu korunur (P1/P2A GUI testleri değişmez).

## Sınırlar (dürüstçe raporlanır)

* Bir görünüşün **kendi içindeki** ölçü yoğunluğu kâğıt büyütmeyle çözülmüyorsa (P2A yerleşim
  motorunun `DIMENSION_UNPLACEABLE` sınırı) çoklu pafta da çözemez: çıktı `NO_FIT` + `TASLAK` olur,
  sahte `READY` üretilmez.
* Otomatik ölçek serbestken tek pafta neredeyse her zaman kazanır; çoklu pafta pratikte
  **sabit ölçek** veya **küçük kâğıt sınırı** durumlarında devreye girer.
* P2B (elle 2D düzenleme) bu aşamada yoktur; yerleşim parametreleri (`offset/side/textT`,
  `TextAnnotation::pos`) zaten kalıcıdır ve P2B için hazırdır.

## Test

`model_maker_drawing_set_test` (52 kontrol, ~4 s):
varsayılan/JSON turu, tek parça READY, orta montaj kâğıt büyütme, tek sayfaya sığmayan montaj →
6 paftalık geçerli set (tüm paftalar çakışmasız), çift yönlü referans/indeks/antet, ölçü kümesi
tek-pafta üretimiyle aynı, determinizm, JSON turu, yedek kapalı → açık hata, dar sınırda
çözülemeyen pafta → `NO_FIT` (READY değil), sabit kâğıt sınırı.

`model_maker_drawing_set_gui_test` (22 kontrol, Qt offscreen):
çok sayfalı vektör PDF (sayfa sayısı = pafta sayısı, her sayfa kendi boyutunda), `pdfinfo`,
`pdftoppm` ile gerçek raster (boş sayfa yok), `pdftotext` ile ölçek/pafta no/ölçü metni,
önizleme- PDF paritesi, diyalog pafta seçici.

Komutlar:

```
cmake -S . -B build-hermes -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_DISABLE_FIND_PACKAGE_OpenCASCADE=ON
cmake --build build-hermes --target model_maker_drawing_set_test model_maker_drawing_set_gui_test
cd build-hermes && ctest -R "drawing_set" --output-on-failure
```
