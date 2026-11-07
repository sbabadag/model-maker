# Iki noktali gorunus (Tekla "view by two points") + MDI — ayrintilar

Kullanim: pencere/gorunus mekanizmasina dokunacaksan (createViewport, swapViewFields,
routeCanvasMessage, pick filter) once bu dosyayi oku.

- Merkez alan `QMdiArea`; ana Win32 canvas `canvasContainer_` KAPATILAMAZ bir alt pencerede
  (`modelSubWindow_`, eventFilter Close'u yutar). canvasContainer_ alt pencereye winId()'den
  ONCE eklenir — native widget sonradan yeniden ebeveynlenirse HWND yeniden olusur ve SetParent
  ile gomulu canvas kaybolur.
- Komut: Gorunum > 2 Noktali Gorunus (Ctrl+Shift+V) / ribbon "2N Gorunus" ->
  `Application::startTwoPointViewCommand()` = workPlanePicking_ akisi + `pointPickPurpose_ =
  TwoPointView` (snap/track/onizleme bedava). 2. noktada `viewFromTwoPoints` ->
  `twoPointViewCallback_` (Qt QTimer::singleShot(0) ile kuyruklar) -> `openTwoPointView`.
  `cancelWorkPlaneCommand` amaci WorkPlane'e sifirlar (yoksa duzlem komutu 2 noktada biter).
- Cekirdek `view_definition.hpp`: right = P1->P2'nin YATAY izdusumu, up = dunya Z, normal =
  right x up; Z farki gorunusu egmez; ayni XY -> nullopt (bip + 2. nokta yeniden). Derinlik
  dilimi `boundsInViewSlab` (varsayilan 1000/1000 mm), `viewFitBounds` (NaN atlar, bos dilimde
  pay). `Camera::setViewBasis(right, up, center)` keyfi taban.
- **v2 (guncel):** `TwoPointViewWidget` ARTIK INCE BIR KAP — icinde `Application::createViewport`
  ile uretilen GERCEK Win32 tuvali vardir (ayni pencere sinifi + ayni `canvasProc`). Boylece
  komutlar/kisayollar/snap/gumball/secim view penceresi icin yeniden yazilmaz.
- `Application` coklu viewport: `views_` (her yuva: canvas/camera/mode/renderer/workPlane/
  viewDef) + `activeView_`. Gorunuse ozgu alanlar (canvas_, camera_, mode_, renderer_,
  workPlane_, viewDef_) HER ZAMAN aktif viewport'a aittir; gecis = `swapViewFields` takasi.
  `renderer_` bu yuzden `unique_ptr<Renderer>` (yuva ile takas edilir).
- `routeCanvasMessage(window, ...)`: `viewIndexOf` ile yuva bulunur. Aktif olmayan tuvalde
  WM_PAINT -> `paintPassiveViewport` (kendi kamerasi, saf GDI, komut onizlemeleri kapali);
  WM_DESTROY -> kaydi siler; fare/tus/odak mesaji -> `activateViewport` + mesaj AYNEN
  `handleCanvasMessage`'a. Surukleme sirasinda (capture baska tuvalde) MOUSEMOVE gecis yapmaz.
  `canvasProc` WM_NCCREATE'te `creatingViewportCanvas_` iken `canvas_` EZILMEZ.
- Aktiflik gecisinde ekran-koordinatli yarim etkilesimler sifirlanir (rotating/panning2D/
  viewCube/selectionFirstCorner/zoomWindow/hover) ve gumball surukleniyorsa iptal edilir;
  ORTAK komut durumu (transformPhase_) korunur -> Move yarimken pencere degistirilebilir.
- Tazeleme: `syncPassiveViewports` ucuz imza (revision + secim + stil) ile diger tuvalleri
  invalidate eder; her tuval mesaji sonrasi + Qt 120 ms zamanlayicisi (menuler tuval disi
  belge degistirir). `mainCanvas()` = aktif degilken `views_[0]->canvas`.
- Derinlik dilimi artik BELGE duzeyinde filtre: `Document::setPickFilter` + `modelIsPickable`
  (drafting'de isabet/pencere/snap `modelIsEditable` yerine bunu kullanir). Aktif viewport
  icin `applyPickFilter()` (yeni belge acilinca da). Secili nesne yine tasinip silinebilir.
- **YON KILIDI:** gorunus yonu iki noktayla tanimli -> `viewRotationLocked()` (viewDef_ var).
  Orta tus surukleme = PAN (rotate degil); `setStandardView`/`R`/`CmdResetView`/`CmdNew` =
  `fitViewport` (yeniden sigdir) + durum notu; ViewCube kapali; GL yalniz ana gorunuste
  (`activeBackend ... && !viewDef_`) ve WM_SIZE GL FBO'yu yalniz ana tuval icin boyutlar.
- Renderer: `DraftView.viewSlab` (nullptr = ana gorunus) model + nokta + SECIM VURGUSU
  pass'larini filtreler (`inViewSlab`), grip tutamaklari cizilmez.
- GUI hedefi CMake'te `WIN32 AND Qt6_FOUND` ile korunur (Linux'ta Qt kuruluyken windows.h
  hatasi vermesin). Linux'ta qt6-base-dev kurulu: Qt dosyalarini MinGW ile soz dizimi kontrolu:
  `-I ~/.hermes/cache/scratch/qtwin -I /usr/include/x86_64-linux-gnu/qt6{,/QtCore,/QtGui,/QtWidgets}`
  (qtwin = indirilen qwindowdefs_win.h); /tmp/qstub'i bu durumda EKLEME (QString cakisir).
- **main.cpp ve qt_main_window.cpp ARTIK SKIP DEGIL:** host Qt 6.10.2 + `qtwin` ile gercek Qt
  basliklari altinda (yukaridaki `-I` listesi) ikisi de temiz gecer. `win_syntax_check.sh`'in
  "SKIP (needs QMainWindow)" satirini kabul etme — yukaridaki komutu elle calistir ve sonucu raporla.
- Testler: `model_maker_view_definition_test` + `run_two_point_view.py` (16) + `run_multi_viewport.py`
  (54; gercek `routeCanvasMessage`/`activateViewport`/`createViewport`/`setStandardView`/
  `fitViewport` govdeleri + SAHTE Win32 kaydedici; mutasyonla dogrulandi =
  `viewRotationLocked` kaldirilinca test duser). `run_view_selection.py` KALDIRILDI.
  Harness tuzagi: onceki bolumde YOK EDILMIS bir tuvali tekrar kullanma — `viewIndexOf`
  nullopt doner, mesaj sessizce `DefWindowProcW`'a gider ve test yanlis nedenle duser;
  ayakta kalan yuvayi `views_[i]->canvas` ile al.
  Qt MDI/GDI cizimi, gercek fare/tus dagitimi ve odak Windows'ta dogrulanmali.
- Gorunus penceresinde SECIM: ekstra kod YOK — ayni `handleCanvasMessage` calisir
  (sol tik ekle/cikar, bos alan temizler, surukleme = window/crossing, Esc, Delete, Enter).
  Disaridan secim yazarken de dilim filtresi gecerli (bkz. `modelIsPickable`).
- **Kayit/gotcha (dort dokunma noktasi):** iki native pencere tek `Application` durumunu
  paylasir. Yeni bir "gorunuse ozgu" alan eklersen (1) `ViewportState`'e alan, (2)
  `swapViewFields`'e `std::swap`, (3) `paintPassiveViewport`'ta pasif tuval icin DRAFT'a
  yaz (`DraftView`'a tasiniyorsa), (4) `syncPassiveViewports` imzasindan cikar/ekle.
  Eksik (2) = deger pencereler arasi sizar; eksik (3) = pasif pencere AKTIF pencerenin
  ayariyla cizilir; eksik (4) = bosuna yeniden cizim. Ortak kalanlar (secim, belge, komut
  fazi) bilerek uyelerde durur.
- **Ornek — gorsel stil (Alt+1..4 / F2 / menu / Ctrl+Shift+1..4) viewport basina:**
  `ViewportState::visualStyle` + swap + `paintPassiveViewport`'ta `view.visualStyle =
  slot.visualStyle` + imzada yalniz PASIF pencerelerin stili. Yeni pencere acildigi andaki
  aktif stille baslar (`createViewport`). Aktif pencerenin stili digerlerini NE degistirir
  NE tazeler. Mutasyon testi: swap satirini sil -> 2 check duser.

---

<!-- SKILL.md'den taşındı 2026-10-19 (gövde sadeleştirme) -->

## Iki noktali gorunuste secilen nokta IS DUZLEMINE IZ DUSURULUR

KULLANICI SARTI: "2 noktayla view aldik; tasimalar kopyalamalar vs projected
noktalardan workplane uzerinden tasinacak."

- TEK KAYNAK: `WorkPlane::projectPoint(Vec3)` (geometry.hpp/.cpp) — noktayi
  duzleme iz dusurur (normal bileseni atilir; bozuk normalde noktaya DOKUNMAZ).
- `updateHover`'da snap cozumunden **SONRA**, `if (viewDef_ && hover_)` kapisiyla
  uygulanir; `application.cpp`'de tam olarak BIR cagri olmali (kopya mantik yok).
  Snap'ten sonra olmasi sart: TAB ile alinan TP adayi da boylece duzlemde toplanir.
- **Neden ekranda hicbir sey oynamaz:** iki noktali gorunusun is duzlemi = gorunus
  duzlemi, ve normali = BAKIS yonu (`viewWorkPlane`, normal = right x up = -bakış).
  Normale gore iz dusum imlecin ALTINDAKI noktayi korur; yalniz derinlik bileseni
  atilir. Yani kullanici ne goruyorsa onu kullanir.
- **OLCULEN REGRESYON** (duzeltmeden once; `tests/run_view_workplane_move.py`
  gercek `SnapEngine::snap3D` + `Camera` ile): baz baska DERINLIKTEKI uye ucuna
  snap'lenip hedef serbest secilince `disp . normal = -1200.000` — nesne derinlige
  kayip gorunusun derinlik diliminden CIKIYORDU. Iz dusum ile `0.000`.
  Uygulamadaki "hedef duzlemi baz noktasindan gecir" (`activePlane.origin =
  reference`) kurali TEK BASINA yetmez: baz noktasi duzlem DISINDA kaldigi surece
  fark duzlem disina cikar.
- Kapsam: gorunus penceresinde TUM nokta secimleri (tasima/kopyalama/dizi/olcek/
  cizim) — kullanici "tasimalar kopyalamalar vs" dedi, ayrim yapilmadi.
- Testler: `model_maker_view_workplane_move_test` (12 kontrol, OCC'siz, Linux'ta da
  kosar) + `tests/run_view_workplane_move.py` (sekil + gercek cekirdek
  davranisi; cekirdek kutuphanesini `build` dizininden linkler). Mutasyonlar:
  satir kaldirma / etkisiz `projectPoint` / kapi kaldirma / snap'ten ONCE uygulama /
  kopya cagri — besi de yakalanir.
- **Mutasyon harness tuzagi (yasadim):** geri alma icin `text.replace("", orig)`
  YAZMA — Python bos string'i HER karakter arasina ekler ve dosyayi megabaytlarca
  bozar (`git diff` +323k satir). Orijinal icerigi bellekte tut, mutasyon sonrasi
  AYNEN geri yaz; her mutasyonu `try/finally` ile sar; sonda `ORIG == disk`
  kontrolunu yazdir. Bozulursa: `git checkout -- <dosya>` + degisikligi yeniden uygula.
- **`mutate_occ_solid_ids.py` UZUN surer (~8-12 dk, 16 mutasyon x ~40 sn derleme).**
  300 sn foreground timeout onu ORTASINDA oldurur; `finally` calismadigi icin kaynak dosya
  (orn. `src/occ_solid_store.cpp`) MUTASYONA UGRAMIS kalir. Sonraki her harness/referans kosusu
  sahte 46/198 hatasi verir ve ctest REGRESYONA benzer gorunur. Kontrol: `git status --porcelain`
  -> ' M src/...' varsa `git restore <dosya>` yap; kaynak temizse sonuc guvenilir. Uzun kosu icin
  background=true + process_manage wait kullan (foreground 600 sn'yi asar).

---

## Iki noktali gorunus (Tekla "view by two points") + MDI

- Komut: Gorunum > 2 Noktali Gorunus (Ctrl+Shift+V) / ribbon "2N Gorunus" ->
  `startTwoPointViewCommand()` = workPlanePicking_ akisi + `pointPickPurpose_ = TwoPointView`.
  2. noktada `viewFromTwoPoints` -> `twoPointViewCallback_` (Qt QTimer::singleShot(0)) ->
  `openTwoPointView`. `cancelWorkPlaneCommand` amaci WorkPlane'e sifirlar.
- Cekirdek `view_definition.hpp`: right = P1->P2'nin YATAY izdusumu, up = dunya Z, normal =
  right x up; Z farki egmez; ayni XY -> nullopt (bip). Derinlik dilimi `boundsInViewSlab`
  (varsayilan 1000/1000 mm), `viewFitBounds`. Kamera: `Camera::setViewBasis(right, up, center)`.
- **v2 (guncel):** `TwoPointViewWidget` ARTIK INCE KAP — icinde `Application::createViewport`
  ile uretilen GERCEK Win32 tuvali var (ayni sinif + ayni `canvasProc`); komutlar/snap/gumball/
  secim view penceresi icin YENIDEN YAZILMAZ.
- Gorunuse ozgu alanlar (canvas_, camera_, mode_, renderer_, workPlane_, viewDef_) HER ZAMAN
  aktif viewport'a aittir; gecis = `swapViewFields` takasi (`renderer_` unique_ptr).
  Yeni bir viewport-basi alan eklerken DORT temas noktasini unutma: yapi alani, `swapViewFields`,
  `paintPassiveViewport`, gecis sifirlamasi.
- Derinlik dilimi BELGE duzeyinde filtre: `Document::setPickFilter` + `modelIsPickable`.
- Aktiflik gecisinde ekran-koordinatli yarim etkilesimler sifirlanir; ORTAK komut durumu
  (transformPhase_) korunur -> Move yarimken pencere degistirilebilir.

Ayrintilar: `references/two-point-view-mdi.md`.
