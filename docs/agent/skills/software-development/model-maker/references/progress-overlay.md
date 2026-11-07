# Ilerleme penceresi (ekranin ortasinda) — dosya ac/kaydet + assembly cizimi

`feature/ui-progress-overlay-20261028` (commit 3480752, base P2D cdd3a6d).

## Mimari: iki katman, tek sozlesme

- `include/model_maker/progress_report.hpp` (Qt'siz): `mm::ProgressReport{stage, done, total}` +
  `mm::ProgressCallback` + `reportProgress()`. `total > 0` = belirli cubuk, `total <= 0` = belirsiz
  (busy). Callback bos olabilir; `reportProgress` sessizce yutar.
- `include/model_maker/progress_overlay.hpp` (Qt): `mm::ui::ProgressOverlay` — **header-only**,
  **Q_OBJECT YOK**. Begin/report/finish + test erisimcileri (`panelRect`, `barBusy`, ...).

## KURAL: overlay'i .cpp'ye tasima

Her GUI test hedefi `src/drawing_sheet_widget.cpp`'yi AYRICA derliyor (ortak kutuphane yok). Yeni bir
`progress_overlay.cpp` eklemek 7+ hedefte kaynak listesi duzenlemesi demek. Q_OBJECT gerekmedigi
(sinyal/slot yok) icin **header-only** tuttuk: sadece `#include` yeter, CMake'e dokunma.

## KURAL: report() olay pompalamali

Is GUI is parcaciginda bloklayarak calisiyor; `QApplication::processEvents(QEventLoop::
ExcludeUserInputEvents)` olmadan cubuk DONAR (pencere bos cizilir). Kullanici girdisi haric tutulur:
islem surerken tiklama islenmez. `Esc` `keyPressEvent`'te yutulur — pencere is bitmeden kapanmaz.

## KURAL: panel EKRANIN ortasinda, pencere tum ekrani kaplar

`owner_->screen()->availableGeometry()` (yoksa primaryScreen) → widget geometrisi = ekran;
panel `(width-panelW)/2, (height-panelH)/2`. Parent pencerenin ortasina koymak istenen davranis DEGIL.
`Qt::Tool | FramelessWindowHint | WindowStaysOnTopHint` + `WA_TranslucentBackground` +
`WA_ShowWithoutActivating`.

## Application tarafi: RAII guard + gercek adim sayisi

`Application::IoProgressSink{begin(title,total), update(done,total,detail), end()}`;
`src/application.cpp` icinde `IoProgressGuard` (anonim namespace) **her cikis yolunda** `end()` cagirir
(istisna dahil). Dosya acmada `total = model sayisi + uc levha sayisi + 1 (bilesen partisi)` —
GERCEK is adedi; sabit 100 gibi uydurma deger kullanma. Acilis sonrasi agir dongu yalniz
`#ifdef MM_HAS_OCC` icinde oldugu icin adim bildirimleri de orada.

## buildDrawingSet imza degisikligi — gizli maliyet

`buildDrawingSet(..., const ProgressCallback& progress = {})` eklemek `DrawingPreviewDialog::
SetBuilder` typedef'ini de degistirir → **tum test lambda'lari** (4 dosya: drawing_p2d_gui_test,
 drawing_p2d_select_move_gui_test, drawing_set_gui_test, paper_editing_gui_test) 4. parametreyi
 almali. Derleme hatasi vermeden once bunlari grep'le: `const DrawingPreferences& p)`.

## Test deseni

- Qt'siz: `tests/drawing_set_progress_test.cpp` — 4 asama sozlesmesi, artan `done`, determinizm ve
  **callback'in ciktiyi degistirmedigi** (`serialize()` birebir). Ilerleme testinin asil degeri bu
  son madde: bildirim kodu cizim geometrisini bozmamali.
- Qt: `tests/progress_overlay_test.cpp` (offscreen) — `geometry() == screenRect()`,
  panel merkezi ±2 px, busy↔belirli gecis, `done > total` kirpilir, Esc ile KAPANMAMA, idempotent
  `finish()`, yeniden `begin()`.
- Yeni GUI testi CMake'te `set_tests_properties(... model_maker_progress_overlay_test ...
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")` listesine EKLENMELI; yoksa offscreen olmadan kosar.
- `QMessageBox` overlay ACIKKEN gosterilmez: once `finish()`, sonra mesaj (ornek: PDF/JSON export).

## Dogrulama (2026-10-28, bu host)

- Tam ctest **133/133** (onceki 131 + 2 yeni), 137 s.
- `src/application.cpp` mingw `-fsyntax-only` hem `MM_HAS_OCC` hem onsuz hatasiz.
- `src/qt_main_window.cpp` bu host'ta derlenemez (Qt+Win32); Windows derlemesi kullanici makinesinde.
- Bilinen sinirlar: iptal dugmesi yok; kisa islemlerde pencere bir an gorunur; kaydetme tek adimli
  oldugu icin belirsiz cubuk.
