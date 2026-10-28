# İlerleme penceresi — dosya aç/kaydet + assembly çizimi (2026-10-28)

Kullanıcı isteği: "dosya açılımı, kaydediliği (kaydetme) ve assembly çizimlerinde ekranın ortasında
bir progress bar window ile göster".

## Davranış (kabul edilen)

- Pencere **tüm ekranı** kaplar ve hafif karartır: işlem sürerken kazara tıklama olmaz.
- Panel **ekranın tam ortasında**. (Parent pencerenin ortası DEĞİL — istenen davranış bu.)
- İçerik: başlık ("Dosya açılıyor", "Kaydediliyor", "Çizim üretiliyor", "PDF yazılıyor",
  "JSON yazılıyor"), aşama etiketi ve çubuk.
- Çubuk iki modda:
  * **belirli**: `total > 0` → değer `done/total` (dosya açmada gerçek iş adedi kadar),
  * **belirsiz (busy)**: `total <= 0` → yalnız aşama etiketi değişir.
- `Esc` pencereyi **kapatmaz** (işlem yarım kalmaz). Tek kapatma yolu işin bitmesidir.
- `report()` çağrısı olayları **pompalar** (`ExcludeUserInputEvents`) — bu olmadan çubuk donar,
  çünkü iş GUI iş parçacığında bloklayarak çalışıyor.

## Dosyalar

| Dosya | İçerik |
|---|---|
| `include/model_maker/progress_report.hpp` | Qt'siz çekirdek sözleşmesi: `mm::ProgressReport` + `mm::ProgressCallback`, `reportProgress()` |
| `include/model_maker/progress_overlay.hpp` | `mm::ui::ProgressOverlay` — header-only Qt widget (Q_OBJECT yok → moc/CMake ek yükü yok) |
| `src/drawing_set.cpp` | `buildDrawingSet(..., const ProgressCallback&)` — 4 gerçek aşama bildirir |
| `src/drawing_sheet_widget.cpp` | `DrawingPreviewDialog`: üretim + PDF + JSON sırasında overlay |
| `include/model_maker/application.hpp` | `Application::IoProgressSink { begin, update, end }` |
| `src/application.cpp` | `IoProgressGuard`: aç/kaydet boyunca RAII, her çıkış yolunda kapanır |
| `src/qt_main_window.cpp` | Sink ↔ overlay bağlantısı (tek yer) |

## İlerleme adımları (gerçek iş adedi, sahte yüzde yok)

- **Dosya aç**: `begin("Dosya açılıyor", 0)` (dosya okuma belirsiz) → okuma bittikten sonra
  `total = model sayısı + uç levha bağlantısı sayısı + 1 (bileşen partisi)`; her profil katısı /
  uç levha / bileşen partisi sonrası `report(++done, total, ...)`.
- **Kaydet**: belirsiz çubuk ("Kaydediliyor") — tek adımlı iş.
- **Çizim (set)**: 4 aşama: görünüşler+ölçüler → kâğıt/ölçek denemesi → tek pafta kararı ya da
  paketleme → "Çizim hazır". Hata yolunda da bildirim tutarlı kalır (0..4, artan).

## Testler

- `model_maker_drawing_set_progress_test` (Qt'siz, 12 kontrol): 4 aşama sözleşmesi, artan `done`,
  determinizm, **callback'in çıktıyı değiştirmemesi** (JSON birebir), geçersiz konu yolu.
- `model_maker_progress_overlay_test` (Qt offscreen, 22 kontrol): panel ekranın ortasında, pencere
  tüm ekranı kaplar, belirli/belirsiz mod geçişi, `done` kırpma, Esc ile kapanmama, idempotent
  `finish()`, yeniden başlatma.

Linux: tam ctest **133/133** geçti (önceki 131 + 2 yeni). `src/application.cpp` mingw ile
`-fsyntax-only` (hem OCC'li hem OCC'siz) hatasız.

## Bilinen sınırlar

- **İptal düğmesi yok** (istenmedi). İş yarıda kesilemez; pencere Esc ile kapanmaz.
- Kısa işlemlerde pencere bir an görünüp kapanabilir (kabul edilen davranış; gecikmeli gösterme
  eklenmedi çünkü tek uzun adımlı işlemlerde pencere hiç görünmezdi).
- Dosya **kaydetme** tek adımlı olduğu için yüzde değil belirsiz çubuk gösterir.
- Windows görsel kabulü kullanıcıda: ZBook'ta `model-maker.exe` ile aç/kaydet + Paper Çizim akışı.
