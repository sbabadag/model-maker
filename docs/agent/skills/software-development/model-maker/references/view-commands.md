# View Commands — model-maker

## TEK KAYNAK KOMUT TABLOSU — menü == ribbon (v11, 2026-10-23, BAĞLAYICI)

Kullanıcı: "Menüler çok karıştı hepsini kendi ilgili ribbon ve menülerine taşı".
- Tüm komutlar `QtMainWindow::commandCategories()` (qt_main_window.cpp) içinde
  kategori kategori tanımlanır: Dosya*, Çizim, Düzenle, Çelik, Konstrüksiyon,
  Görünüm, Çalışma Düzlemi, Görünüşler, Grid, Pencere* (*=tabTitle boş, yalnız menü).
- `createMenus()` ve ribbon sekmeleri (`createToolbar`) ile Quick Launch AYNI
  tablodan üretilir. createMenus/createToolbar'a ELLE `addAction`/düğme EKLEME —
  yeni komut yalnız doğru kategoriye `uiCmd/uiGroup/uiSep` ile eklenir.
- Çelik = Bolt, Kaynak, End Plate, Fin Plate, Component Kataloğu (registry'den),
  Tekla Component Bridge (içe aktar/native/şablon). Konstrüksiyon = konstrüksiyon
  düzlemi + kesmeler (Fitting/Plane/Line/Polygon/Part Cut).
- Kısayol: `uiKey(..., bind=true)` yalnız ana menü eylemine QKeySequence bağlar;
  ribbon açılır listesinde metin (\t) olarak gösterilir (aynı tuşa iki eylem =
  belirsiz kısayol). Ctrl+Z/Y/N/O/S, Alt+1..4, F7, R, D yalnız GÖSTERİM (QShortcut /
  WM_KEYDOWN zaten bağlı).
- `CommandCategory` header'da nested forward-decl; UiEntry cpp'de anonim namespace
  (ana .cpp'de tanımlı olduğu için -Wsubobject-linkage uyarısı çıkmaz).
- Test: `tests/run_view_menu.py` tabloyu yorum işaretleyicilerinden (`// ---- ÇELİK` vb.)
  bloklara böler; kategori yerleşimi + her komut tabloda TAM 1 kez + menü/ribbon
  elle komut yok. `tests/mutate_view_menu.py` 11/11.
- Windows'a özel qt_main_window.cpp Linux'ta sözdizimi kontrolü:
  `~/.hermes/cache/scratch/qwinstub/QtGui/qwindowdefs_win.h` (içerik `#include <windows.h>`)
  + `x86_64-w64-mingw32-g++ -std=c++20 -fsyntax-only -Wall -Wextra -DUNICODE -D_UNICODE
  -I <qwinstub> -I include -I /usr/include/x86_64-linux-gnu/qt6{,/QtCore,/QtGui,/QtWidgets}
  src/qt_main_window.cpp` (~2-4 dk; 600 sn sınırına dikkat).

Aşağıdaki v10 "tek Görünüm menüsü" anlatısı TARİHÎ: sonra kategoriler ayrıldı
(Görünüm / Çalışma Düzlemi / Görünüşler / Grid), v11 bunları tabloya bağladı.

> 2026-10-19 tarihinde SKILL.md gövdesinden taşındı; içerik birebir korunmuştur.
> Tetikleyici ve kısa kural SKILL.md'deki referans dizininde.

## GORUNUM KOMUTLARI TEK MENU/RIBBON + PLAN GORUNUSU (Z kotu) (v10)

KULLANICI SARTI (iki parca): "view eklemeye PLAN XY de belirtilen Z
koordinatinda ekle" + "tum goruntu ile ilgili komutlar bir menu ve ribbonda
toplansin".

**A) PLAN gorunusu (Z kotu) — gorunus ekleme akisi kot alir.**

- TEK KAYNAK `mm::viewFromPlanLevel(level, above, below)` +
  `mm::isPlanView(v)` (view_definition.hpp). Gorunus duzlemi XY'ye PARALEL:
  `right = {1,0,0}`, `up = {0,1,0}`, `normal = {0,0,1}` (izleyici yukaridan),
  `length = 0` (P1-P2 hatti YOK). Iki noktali elevasyonda `up = dunya Z` ve
  `normal` YATAYdir — bu ayrim `isPlanView` = `|normal.z| > 0.9` ile yapilir
  (kalici alan DEGIL, turetilmis).
- **KOT NE GORUNUR'u belirler, ekran yonunu degil.** Plan gorunusunde derinlik
  dilimi DUSEYdir: `depthFront` = kotun USTU (+Z), `depthBack` = kotun ALTI.
  Varsayilanlar `kDefaultPlanDepthAbove = 3000` / `kDefaultPlanDepthBelow = 1000`.
  Bu yuzden bir kot parametresi plan gorunusunde ANLAMLIDIR; dikey (XY'ye dik)
  gorunuste Z yalnizca bos-dilim fit payini etkiler, yani oraya kot eklemek
  neredeyse kozmetiktir.
- **KOT 0 veya NEGATIF OLABILIR** → `requestDoubleParameter` (>0 sarti)
  KULLANILAMAZ; ayri `Application::requestLevelParameter(prompt, initial)`
  (`used == out.size() && isfinite` yeter; `value <= 0.0` kapisi YASAK).
- `Application::startPlanViewCommand()`: (1) `twoPointViewCallback_` yoksa bip +
  cikis; (2) `workPlanePicking_` ise `cancelWorkPlaneCommand()` (yarim kalmis
  nokta secimi arka planda asili kalmasin); (3) kotu edit box ile sor
  (iptal/bozuk deger = gorunus ACILMAZ); (4) `viewFromPlanLevel` ile uret ve
  AYNI `twoPointViewCallback_`'i cagir → Qt yeni MDI penceresi acar. Ikinci ve
  ucuncu gorunus TIPI icin ayri creation akisi YAZMA; callback'i paylas.
- **`viewFitBounds` TUZAGI:** plan gorunusunde `length == 0` oldugu icin bos
  dilimde kutu TEK NOKTAYA coker (min == max) ve `fit3D` anlamsiz olcek uretir.
  Cozum: `padInPlane = (v.length > 0.0) ? 0.0 : pad` — plan'da x/y'ye de pay,
  elevasyonda YALNIZ Z payi (eski davranis birebir korunur).
- Baslik/etiket ayrimi: `openTwoPointView` adi `Plan Gorunusu N` ve basligi
  `plan, Z = %1 mm` (elevasyonda `%1 mm, XY'ye dik`); `TwoPointViewWidget::editDepth`
  plan'da `Kotun USTU` / `Kotun ALTI`, elevasyonda `On/Arka derinlik` sorar.

**B) Tum gorunum komutlari TEK menu + TEK ribbon sekmesi.**

- Gorunum MENUSU kendi icinde KAPALI olmali: `createMenus()` icinde
  `windowMenu` olusturulduktan SONRA `viewMenu->addAction(...)` CAGRISI
  KALMAMALI. OLCULEN ESKI DAGINIKLIK: grid komutlari (`Yapi Gridi Olustur`,
  `Yapi Gridleri`) `windowMenu` blogundan sonra `viewMenu->` ile ekleniyordu —
  yani Gorunum menusunun sonunda ama koddan kopuk. Komutlar gruplanir:
  yakinlastirma/mod, yon (alt menu), sifirlama/donus merkezi, calisma duzlemi +
  hayalet, gorunus pencereleri (2 noktali + plan), grid, teshis.
- Ribbon "Gorunum" sekmesi menüyle AYNI kumeyi icermeli; 12 dugme tek sirada
  sigmaz → `QToolButton::InstantPopup` ile IKI acilir menu dugmesi ("Gorunus" =
  7 standart yon, "Grid" = 2 grid komutu) ve `QFrame::VLine` ince ayiricilar.
  `QToolButton::menu-indicator { image: none; }` — yoksa bos ok isareti cikar.
- Kisayol: 2 noktali `Ctrl+Shift+V`, plan `Ctrl+Shift+P` (catisma yok).

Testler: `tests/view_definition_test.cpp` 55 kontrol (plan cekirdegi: eksenler,
normal, NaN kot, kot-ustu/alti dilim, bos-dilim fit kutusu, elevasyon
regresyonu) + `tests/run_view_menu.py` (ctest `model_maker_view_menu`): sekil
(menu kapali + ribbon menüyle ayni kume + baslik/etiket ayrimi) + GERCEK
`startPlanViewCommand`/`requestLevelParameter` govdeleri sahte Application +
GERCEK `view_definition.hpp` ile (callback yok / iptal / 0 ve NEGATIF kot /
bozuk deger / yarim secim temizligi). Mutasyon `tests/mutate_view_menu.py`
8/8 (menu yanlis komuta baglanir, ribbon dugmesi silinir, grid komutlari
geri dagitilir, baslik kotu gostermez, kot >0 sarti eklenir, secim
temizlenmez, plan duzey olmaktan cikar, fit payi kaldirilir) — mutator
`cc` turunde C++ testini yeniden DERLEYIP kosar, dosyalari bayt-birebir geri
yazar. ctest 29/29.

HARNESS NOTU: uretilen TU gercek `view_definition.hpp`'i include eder
(portatif: `document.hpp`/`geometry.hpp` Windows basligi tanimaz) — boylece
`isPlanView` ve `viewFromPlanLevel` KOPYA mantik olmadan sinanir.

---

## Varsayilan 3B gorunum acisi — "xy yere dik" duzeltmesi

KULLANICI SARTI: "goruntumuz default geldiginde yanlis kamera acisinda geliyor
xy yere dik ... 3d view a basincada default boyle yapmali."

- **TEK KAYNAK `kIsoYaw/kIsoPitch/kIsoRoll`** (src/camera.cpp, anonim
  namespace): ISO acisi. `Camera::reset()` (varsayilan) VE
  `setView(StandardView::Isometric)` (3B gecis / "3D view") AYNI sabitlerden
  beslenir — "varsayilan == 3D view" tek acida. reset()'te ayri bir aci
  literal'i YAZMA (yoksa iki yol zamanla ayrisir).
- **Gorunus yonu testi (R_ private):** `viewTransform`'tan yeniden kurulur.
  R_ = world->camera (row-major): row0=camera-X(ekran saga), row1=camera-Y
  (ekran yukari), row2=camera-Z(izleyiciye dogru). Ekran-yukari dunya vektoru
  = `(viewTransform(e_x).y, viewTransform(e_y).y, viewTransform(e_z).y)`;
  bakis yonu = `.z` bilesenleri (grid view-direction kuraliyla ayni kalip).
- **Eski kusur:** reset() = `buildRotation(-0.55, 0.45, 0)` ekran-yukarisini
  +Y yapiyordu → XY duzlemi yere DIK (dusey) gorunuyordu. Olcum:
  screenUp=world(-0.227, 0.900, -0.371). Yeni (ISO): screenUp=world(-0.408,
  -0.408, 0.816), viewDir=(-0.577,-0.577,-0.577) — Z yukari, izleyici
  on-sag-ust koseden.
- Yon kurali: ekran-yukarinin Z bileseni BASKIN ve POZITIF; bakis yonunun Z
  bileseni NEGATIF (yukaridan bakis). Standart gorunusler (Top/Bottom/Front/
  Back/Left/Right) etkilenmez — sadece reset() degisti.
- Test: `model_maker_camera_default_orientation_tests`
  (tests/camera_default_orientation_test.cpp; ctest): +Z baskin/pozitif,
  yukaridan bakis, reset()==Isometric, Y-baskin regresyonu.
  `orbit_pivot_test` reset()'i YALNIZ "non-trivial baslangic rotasyonu"
  olarak kullanir (aciya duyarli DEGIL) — reset() acisini degistirmek onu
  bozmaz.

---

## Acilis varsayilani: 3B + Solid + GL (korumali)

- Qt kabugu `Application::run()`'i CAGIRMAZ; acilis `QtMainWindow::showEvent` QTimer'inda
  `applyStartupDefaults3D()` (toggle3DView + Solid + `tryEnableStartupGpu`). `run()` da ayni
  fonksiyonu kullanir. `resizeEmbeddedCanvas` her resize'da `tryEnableStartupGpu()` cagirir.
- `tryEnableStartupGpu`: canvas 0x0 ise ERTELE; tek deneme; init basarisiz/istisna -> GDI +
  durum cubugunda not. Guard dosyasi `model-maker-gl-startup.guard` (exe yaninda) GL
  denemesinden once yazilir, ilk gercek GL karesinde (model varken) veya temiz kapanista
  silinir; sonraki acilista duruyorsa o acilis GDI (F6 ile elle GL). `MM_FORCE_GDI=1` kacis.
- Gecmis: acilista otomatik GL bir kez "PC donmasi" sanilip geri alinmisti (962720b); asil
  neden acilista otomatik GRID olusturmaydi (2562b57). Acilista grid OLUSTURMA, zoom-extents
  CAGIRMA (0x0 canvas'ta kamera bozulur).
- OpenGL backend: ensureFbo/glClearColor context CURRENT iken (initialize icinde
  wglMakeCurrent(nullptr)'dan ONCE); shutdown sirasi: makeCurrent -> cleanupGL -> context sil
  -> DC birak -> pencere yok et. renderLines 0 boyutta false doner.
- Test: `python3 tests/run_startup_gpu.py build` (ctest model_maker_startup_gpu, 24 kontrol,
  gercek metot govdeleri + sahte Win32/backend; mutasyonla dogrulandi). Gercek surucu/WGL
  davranisini KANITLAMAZ.

---

## Yuzeye Gore Gorunus (Tekla "view to surface")

Yuz hover'inda vurgulanir, tik -> o yuzun duzlemine DIK yeni MDI gorunusu. Derinlik
varsayilani **100/100 mm** (`kDefaultFaceViewDepth`), `kDefaultViewDepth` (1000) DEGIL.

- Cekirdek: `viewFromFace(point, faceNormal, memberAxis)` (view_definition.hpp) — cerceve
  UCS'nin yuz kuraliyla AYNI: `Ucs::fromFaceAndMemberAxis` (X = uye ekseninin yuz duzlemine
  izdusumu, Y = N x X, izleyici tarafi = N). Yatay yuz -> `isPlanView` true (baslik plan).
  `length = 0` -> iki noktali gorunusun "%1 mm" basligi yerine derinlik yazar.
- Akis: `Application::startViewToSurfaceCommand()` + `PointPickPurpose::ViewToSurface`;
  hover calisma duzlemi araciyla AYNI makineyi kullanir (`pickSceneFaceAt` +
  `planarFaceTriangleGroup` -> `hoverFaceModel_/hoverFaceIndices_`), ama sonuc UCS degil
  `ViewDefinition` -> `twoPointViewCallback_` -> `openTwoPointView`. UCS DEGISMEZ.
- Uc yerde `pointPickPurpose_` kontrolu gerekir (biri atlanirsa hover/snap bozulur):
  `refreshHoverCandidate` (yuz raycast dali), `updateHover` (erken cikis) ve mouse-move
  dali. Esc `cancelWorkPlaneCommand()` ile iptal eder.
- Test: `tests/view_face_test.cpp` (21 kontrol, OCC'siz; duvar/ust/uc yuzu, sag-el
  ortonormal, 100/100 varsayilan, NaN/-5 -> varsayilan, egik uye ekseni izdusumu).
