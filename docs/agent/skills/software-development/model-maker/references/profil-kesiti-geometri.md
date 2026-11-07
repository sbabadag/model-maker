# Profil Kesiti Geometri — model-maker

> 2026-10-19 tarihinde SKILL.md gövdesinden taşındı; içerik birebir korunmuştur.
> Tetikleyici ve kısa kural SKILL.md'deki referans dizininde.

## Profil kesit cercevesi — "ALT BASLIK her zaman XY'ye paralel"

KULLANICI SARTI (profil cizimleri): kesitin **genislik ekseni YATAY** olmali → baslik plakalari
XY duzlemine paralel, govde dusey. TEK KAYNAK: `mm::profileSectionFrame(axis)` (geometry.hpp/cpp)
= {width = normalize(Z x axis), dusey uyede +X; height = axis x width; axis}. `extrudeProfileSolid`
hedef gp_Ax3'u ve `gumballUpdateFrame` (profil dali) AYNI fonksiyondan beslenir — ikinci bir
kural/kopya yazma.

- **OCC tuzagi (olculdu):** iki argumanli `gp_Ax3(P, N)` kurucusu X eksenini N'in EN KUCUK
  koordinatina gore keyfi secer (gp_Ax3.cxx'teki A/B/C karsilastirmasi). Sonuc: +X/+Y
  dogrultusundaki kirisler 90° YATIK gelir (genislik Z'ye, yukseklik Y'ye → basliklar dusey,
  govde yatay), plan caprazi uyeler ise DOGRU gelir → gorunuste tutarsizlik. Cozum: cerceveyi
  `gp_Ax3(P, N, Vx)` ile ACIKCA kur; OCC'nin secimine guvenme.
- **Eski dosyalar kendiliginden yenilenmez:** sec → Ozellikler paneli → Rotasyon = 0 →
  **UYGULA** (`setSelectedEntityProfileRotation` kati kendi ekseninde yeniden uretir).
  Kullanici 90° vererek "duzeltmis" olabilir; duzeltmeden sonra 0° dogrudur.
- **Egik uye:** baslik uyeye dik kalir (celikte standart) — "XY'ye paralel" yalniz YATAY uyeler
  icin tam saglanir; govde her durumda duseydir (genislik ekseni yatay).
- Testler: `model_maker_section_frame_test` (ctest, OCC'siz) + `tests/occ_smoke.cpp` icindeki
  "OCC SECTION-FRAME OK" blogu (5 yon, kapi). Probe:
  `~/.hermes/cache/scratch/section_flange_probe.cpp` (once/ sonra: +X kirisi dy=200/dz=100 →
  dy=100/dz=200).

---

## Profil kesiti REFERANS NORMALI — "UST BASLIK workplane ile paralel" (v2)

KULLANICI SARTI (v2, v1'i GENISLETIR): "profil cizilirken profil ust basligi workplane ile
paralel olacak." v1 dunya XY'ye sabitti; artik baslik plakalarinin paralel olacagi duzlemin
NORMALI cagirandan gelir ve **katiyla SAKLANIR**.

- TEK KAYNAK: `mm::profileSectionFrame(axis, referenceNormal = {0,0,1})` — `width =
  normalize(n x axis)`, `height = normalize(axis x width)`. n = dunya Z iken sonuc v1 ile
  BIREBIR ayni (mevcut 16 kontrol degismeden gecer). Kademeli dusme: (1) n, (2) n ∥ axis ise
  dunya Z kurali `{-ay, ax, 0}`, (3) o da dejenere ise (dusey uye) +X.
- SECIM TEK KAYNAK: `mm::profileSectionReferenceNormal(plane, view)` (view_definition.hpp).
  Aktif IS DUZLEMININ normali; **ANCAK iki noktali gorunusun OTOMATIK gorunus duzlemi ise
  dunya Z** (normaller ayni: `dot > 1-1e-9`). Yoksa elevasyonda cizilen her kirisin basligi
  BAKISA paralel olur ve kiris levha gibi "yatak" gorunurdu. Gorunus icinde FARKLI bir UCS
  kurulduysa o kullanilir.
- **KALICILIK: `EntityProperties.sectionNormalX/Y/Z` (varsayilan 0,0,1) + MMW6.** MMW5 ve
  oncesi aynen okunur (alan okunmaz → varsayilan = eski davranis). Yeni props alani eklerken
  BU DESEN: alan + varsayilan + surum kapisi; elle MMW3 dosyasi yazip yukleyerek geriye donuk
  uyumlulugu GERCEKTEN olc (tests/section_normal_test.cpp).
- **DONUSUMLERDE NORMAL DE DONER** — yoksa gumball cercevesi katidan kayar. Kural: duzlem
  normali `n' = normalize(transpose(M^-1) * n)`. Uygulandigi yerler: `Document::transformModels`
  (linearInverse), undo/redo `Transform` dali (forward: linearInverse / geri: linear),
  `polarArray2D` (Z etrafinda), `mirrorModel2D` (XY yansimasinin dogrusal kismi). Saf OTELEME
  (moveModels, linearArray2D) normali degistirmez.
- Uygulama baglantisi: uretim referansi `Application::profileSectionReference()`'ten gelir ve
  `solidProps.sectionNormal*`'a yazilir; yeniden uretim (rotasyon UYGULA, profil ucu tasima)
  SAKLANANI kullanir; `gumballUpdateFrame` saklanan normali okur. `application.cpp`'de
  referanssiz `profileSectionFrame(ex)` cagrisi KALMAMALI.
- **HARNESS TUZAGI:** `run_gumball_interaction.py` `gumballProject..draftView` ARASINI cekip
  sahte bir `Application`'da derler. `profileSectionReference()` oraya yazilirsa `workPlane_`/
  `viewDef_` bulunamaz ve test DERLENMEZ. Gumball metotlarina AIT OLMAYAN yardimcilari o
  dilimin DISINA koy (bu yuzden `gumballProject`'ten hemen once durur).
- Test beklentisi inceligi: n uye eksenine DIK DEGILSE `height` tam olarak n DEGIL, n'in
  eksene DIK bilesenidir (`n - (n·axis)axis`). `width` ise HER ZAMAN n'e diktir. Testi boyle
  yaz; "height == n" iddiasi egik normallerde yanlis olur.
- Testler: `model_maker_section_frame_test` (24, referans normal vakalari dahil) +
  `model_maker_section_normal_test` (21: secim/gorunus kapisi/MMW6 round-trip/MMW3 geriye
  donuk/rotate+undo+redo/olcek/polar/ayna) + `tests/run_profile_section_reference.py` (20,
  uygulama baglantisi kaynak-sekli) + `tests/occ_smoke.cpp` "OCC REFERENCE-NORMAL OK" blogu
  (GERCEK OCCT: egik duzlemde dy=200 (h), dz=100 (b)). Mutasyon: `tests/mutate_section_normal.py`
  (6 mutasyon; dosyalari bayt-birebir geri yazar — `replace("", orig)` tuzagina dusmez).
- OCCT linki (bu host): `g++ -std=c++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade
  tests/occ_smoke.cpp src/occ_geometry.cpp src/occ_bridge.cpp src/geometry.cpp -lTKernel ...`
  — `src/occ_bridge.cpp` UNUTULURSA `mm_occ_solid_box/free/version` undefined kalir.
- **Qt dosyalarini capraz kontrolden gecirmek icin `qtwin` hazir olmali:** host Qt
  basliklarinda `QtGui/qwindowdefs_win.h` YOKTUR (Windows'a ozel) ve mingw Q_OS_WIN
  tanimladigi icin `qwindowdefs.h` onu ister. Indir ve **`qtwin/QtGui/`** altina koy
  (kok dizine koymak YETMEZ — `#include <QtGui/qwindowdefs_win.h>`):
  `curl -fsSL -o ~/.hermes/cache/scratch/qtwin/QtGui/qwindowdefs_win.h
  https://raw.githubusercontent.com/qt/qtbase/6.10/src/gui/kernel/qwindowdefs_win.h`.
  `qtwin` scratch temizliginde silinir; SKIP gorursen once bunu yeniden kur.
- Windows capraz kontrol: `scripts/win_syntax_check.sh` bu repoda YOK; sweep'i elle yap:
  `for f in src/*.cpp; do x86_64-w64-mingw32-g++ -std=c++20 -fsyntax-only -DUNICODE -D_UNICODE
  -I include -I /tmp/qstub "$f"; done` → 18 OK; `main.cpp`/`qt_main_window.cpp`/`two_point_view.cpp`
  GERCEK Qt basliklariyla (`-I ~/.hermes/cache/scratch/qtwin -I /usr/include/x86_64-linux-gnu/qt6{,/QtCore,/QtGui,/QtWidgets}`,
  qstub EKLEMEDEN) → hepsi temiz. Yani "SKIP" kabul edilmez.
- **OCC'li TU'lar (`occ_geometry.cpp`, `occ_bridge.cpp`, `tests/occ_smoke.cpp`) cross
  kontrolunde `MM_HAS_OCC` verilmezse BOS derlenir — kod hic kontrol edilmemis olur.**
  Verildiginde ise Linux OCCT basliklariyla `-std=c++20` ALTINDA PATLAR: `M_PI`/`M_PI_2`
  `__STRICT_ANSI__` yuzunden tanimsizdir (hata OCCT basliklarinda: `gp_Vec2d.hxx`,
  `gp_Dir.hxx`). Cozum: OCC TU'lari icin **`-std=gnu++20`** kullan
  (`-std=gnu++20 -fsyntax-only -DMM_HAS_OCC -I include -I /usr/include/opencascade`).
  Native `g++` ile de ayni dosya temiz gecer; gercek kanit yine OCCT ile DERLEYIP
  CALISTIRMAKTIR.

---

## Profil kesiti YERLESIMI (v4) — "ust Flansin USTUNDEN cizilmeli"

KULLANICI SARTI (v4; v1 yonelim, v2 referans normali, v3 v2+konum): "ust
Flansin USTUNDEN cizmiyor ust flansin MERKEZINDEN ciziyor". v2'ye kadar kesit
uye ekseni etrafinda ORTALANMIS uretilirdi; v3'te ust flansin/plakanin
ORTA-KALINLIK duzlemi oturmustu (kullanici bunu "merkezden ciziyor" diye
REDDETTI). Artik kesit ne ortalanir ne flans ortasindan gecer:

- **UST YUZEY uye eksenine (yerel y = 0) oturur**: eksen cizgisi kesitin en
ustunden gecer ve TUM kesit cizginin ALTINA sarkar (Tekla "position: top of
section"). Baslik paralelligi (v1/v2) DEGISMEZ; degisen yalnizca DUSEY
KONUMdur.
- TEK KAYNAK `mm::profileSectionTopY(profile)` (profile_database; v3'te
`profileSectionTopPlateCenterY` idi): I-kesit/kutu `h/2`, yuvarlak
`max(w,h)/2`. **Dolu/bos (ici bos mu) ayrimi KALKTI** — ust nokta her iki
durumda da DIS yuzeydir; eski `h/2 - tf/2` / `h/2 - t/2` / `outerR - t/2`
kollari SILINDI (geri gelirse kullanici sikayeti tekrar olusur).
- Kesit turu (rakamdan onceki harf kumesi) `occ_geometry.cpp` icinden CIKARILDI:
`mm::classifyProfileSection` (profile_database) — kati uretimi ve yerlesim ayni
kaynaktan; listeyi ikinci kez yazma. `W` icin `rfind("W",0)==0` kalibi
(`== "W"` DEGIL) korunmali, yoksa "WABC" gibi adlar sinif degistirir.
- **Kaydirma profil DONDURMESINDEN ONCE** uygulanir (`gp_Trsf` translation,
sonra sectionRotation): boylece profil donmus olsa da eksen hep kesitin
USTUNDEN gecer (Tekla'da position profil yerel eksenlerinde tanimlidir).
- Olcum (gercek OCCT, `tests/occ_smoke.cpp` "OCC SECTION-PLACEMENT OK"):
IPE200 +X (0,0,0)->(100,0,0) → z = **-200.00 .. 0.00** (ust yuz Z=0);
KKR30*3 → z = **-30.00 .. 0.00**; diyagonal KKR bbox z = -30..0. Genislik
ortalanmis kalir (y = ±50). Portatif formul: IPE200 100.0, KKR30*3 15.0,
CFCHS127x2.5 63.5, ROD20 10.0. (v3 degerleri: 95.75 / 13.5 / 62.25 / 10.0.)
- Tuzak (yasandi): eski "OCC EXTRUDE-DIAG" kontrolu kesiti ORTALANMIS varsayip
`zMin<-16||zMax>16` bakiyordu → yerlesim degisince sessizce "HATA" yazip
yine de OK basiyordu (exit 0!). Beklenti yeni geometriye guncellendi VE
fatal yapildi (`return 1`). Kural: bir kontrol gercekten dusmuyorsa o kontrol
yoktur; "probe yine de calissin" yorumlu HATA print'ini oldugu gibi birakma.
- Testler: `tests/profile_database_test.cpp` (portatif: siniflandirma + formul),
`tests/occ_smoke.cpp` (gercek OCCT geometrisi), `tests/mutate_section_placement.py`
(4 mutasyon: ortalanmis / ters yon / yarim / **v3 flans ortasi (h/2 - tf/2)**
→ dordu de yakalanir; betik IKI dosyayi da mutasyona ugratabilir ve bayt-birebir
geri yazildigini dogrular). **Eski cizimler kendiliginden yenilenmez** —
UYGULA/uc tasima ile uretilen katilar saklanan eksenden yeniden uretilir ve
yeni yerlesimi alir.

---

## Belge duzeyinde donusum katmani (rotate/scale)

- Belgede eskiden yalniz `moveModels` vardi. Eklendi: `transformModels(indices, pivot,
  linear3x3, linearInverse)` — v' = pivot + linear*(v-pivot). `rotateModels` (Rodrigues,
  ters = transpoze) ve `scaleModels` (diagonal, ters = 1/f; f sifira kelepcelenir) bunun
  uzerine kurulu. `WireframeModel::affineAbout(center, linear)` eklendi.
- Undo: `UndoOp::Kind::Transform` (pivot + linear + linearInverse). undo/redo ters matrisi
  uygular; `rebuildDerivedState()` bounds/spatial'i zaten yeniler. Profil ekseni
  (axisFrom/To) da donusur. Kayit yoksa op DUSER -> pushSnapshot SART.
- Probe: `~/.hermes/cache/scratch/gumball_transform_probe.cpp` (rotate90Z/undo/redo/scale/
  pivot/profil ekseni).
- OCC katilari transform edilmez (mevcut Move ile ayni davranis, bilinen sinir).
  Uygulama koordinatlari world; object-local cerceve YOK.
- **B-Rep sahipligi + surumlu geometri undo/redo (Phase 3.1/3.2A/3.2B):**
  `Document` sahipli opak `OccStore` (OCCT'siz arayuz) + `OccSolidStore` (gercek OCCT,
  constructor injection) + `UndoRecord.geometryBefore/After` + iki fazli atomik undo.
  Application'da AYRI `occShapes_` haritasi YOK. Detay: `references/occ-geometry-ownership.md`.
  Yeni OCC yolu eklerken: (a) `grep occShapes_` sifir olmali, (b) sekli `addStyledModel`
  SONRASI kimlikle yaz, (c) replaceModel/putOccSolid'den ONCE undo kaydi ACIK olmali.

---

## Profil uc tutamaklari GORUNUS DUZLEMI pencerelerinde de gorunur

Kullanici: "plan goruntulerinde profil uzerindeki tasima noktalari gozukmuyor". Kok neden:
renderer'daki tutamak blogu `mode == EditMode::View3D && !draft.snapOnly && !draft.viewSlab`
istiyordu. Plan (Z kotu) ve iki noktali gorunus pencereleri bir gorunus duzlemi (`viewSlab`)
TASIR → tutamaklar orada HIC cizilmiyordu (plan/ikincil pencereler `EditMode::View3D`'dir;
SLOT kurulumu `slot->mode = EditMode::View3D`).

- KURAL: tutamaklar View3D'de `viewSlab`'den BAGIMSIZ cizilir. ANCAK dilim DISINDAKI secili
  katilar o pencerede gorunmedigi icin tutamaklari da cizilmez:
  `if (draft.viewSlab && (gi >= gripBounds.size() || !boundsInViewSlab(gripBounds[gi], *draft.viewSlab))) continue;`
  (`const auto& gripBounds = document.modelBounds();` dongu DISINDA bir kez alinir.)
- Test: `tests/run_grip_slab_views.py` (ctest `model_maker_grip_slab_views`; kaynak sekli — yorumlari
  AYIKLAYIP kodda `!draft.viewSlab` YOKLUGUNU arar, aksi halde kendi aciklama yorumuna takilir) +
  `tests/mutate_grip_slab_views.py` (3/3 yakalar).

---

## Profil uc tutamaklari (grip edit)

- **Secili** profilli katilarin (faces + profileName dolu) iki ucuna 3B gorunumde tutamak cizilir:
  sari=from, mor=to (`Renderer`'da `drawGripMarker`, taban katmaninda). Secili olmayanlarda gorunmez.
- Akis (3 adim): **Alt+tik** tutamaga basinca move modu acilir (aktif: beyaz halka);
  **1. tik** tasima baslangici (baz), **2. tik** destinasyondur. displacement = hedef - baz;
  secilen uc o vektorle tasinir, sabit uc yerinde kalir (`Application::profileGripClick` ->
  `reExtrudeProfileGrip`, **OCC-gerekli**). Snap `SnapEngine::snap3D` ile Move ile ayni yoldan.
- Move modu boyunca **track line** baz->imlec olarak cizilir (noktali sari) ve **snap + track
  sistemi aktif** (grid/object snap marker'i + gecici izleme noktalari/TP, izleme cizgileri,
  turetilmis 'sanal cakistirma' guide'lari, polar tracking, ortho). Move `updateHover`'i kullanir
  (dogrudan snap3D DEGIL) — tum bu katmanlar orada hesaplanir; `gripReferencePoint()` referansi
  (basePicked ? basePoint : movingEnd) verir. Renderer bu katmanlari `gripMoveActive` iken cizer.
- Durum `Application::profileGrip_` ({solidIndex, endIsTo, fixedPoint, basePicked, basePoint,
  trackFrom, cursorPoint}); DraftView'e `activeGripSolid/activeGripEndIsTo/gripMoveActive/...` tasinir.
- Undo: `pushUndoSnapshot()` + `replaceModel` (tek indeks, delta=0) + `occShapes_` guncellenir.

---

## Profil secimi Props panelinde (ribbon cubugu KALDIRILDI)

KULLANICI SARTI (v6, uc parca): "sol ust ribbon daki profil secimi kaldirilsin",
"sagdaki props ta hangi ozellikler varsa ona gore profil cizilsin direk",
"secim yapip props penceresinden de degisiklik yapilan satirda entera basinca da
direk ozelligi degissin".

- `createToolbar()` icindeki 142 satirlik ayri profil toolbar'i (`profileBar`,
  `profileToolbar`, kategori baslikli combo, ipucu QLabel, `addToolBarBreak`)
  TAMAMEN silinir. `createToolbar` icinde `app_.profileNames()` cagrisi KALMAMALI.
- TEK KAYNAK: Props panelindeki `propsProfile` ("Profil (aktif)"):
  * oge 0 = `profileNoneLabel()` yer tutucusu, **UserRole = bos QString** ->
    profilsiz (duz cizgi); diger ogelerde UserRole = profil adi.
  * `comboProfileName(combo)` (file-static, qt_main_window.cpp) adi **oge verisinden**
    okur; veri yoksa (elle yazilan ad) metne duser ve kirpar. Kolon butonu da bunu kullanir.
  * Uygulama yolu TEK: `applyProfileField` -> secim YOKKEN `setPendingProfileName`
    (cizim), secim VARKEN `assignProfileToSelection`. IKISI birden cagrilir; boylece
    panelde gorunen profil her zaman cizilecek profildir.
  * ENTER: liste secimi `textActivated` + `lineEdit()->returnPressed` AYNI yolu
    cagirir. Malzeme satiri da `returnPressed`; rotasyon `editingFinished` (ENTER)
    ile uygular (UYGULA butonu kalir, ayni lambdayi cagirir).
  * Secimden sonra odak tuvale doner (`QTimer::singleShot(0, ... SetFocus(canvas)`)
    — eski ribbon combo'su da yapardi; yoksa L/M/D kisayollari panele gider.
  * `profileSelector_ = propsProfile;` + `setProfilePickerCallback` artik
    `createDockPanels` icinde (X tusu / CmdProfileButton buraya odaklanir).
- **Refresh listeyi `clear()` ILE YENIDEN KURMAMALI.** Eski kod her secim
  degisiminde clear+57 addItem yapiyordu; combo index 0'a dusup kullanicinin
  sectigi AKTIF profil sessizce degisiyordu. Liste bir kez kurulur; refresh yalniz
  degeri secer (secim varsa nesnenin profili, yoksa `pendingProfileName()`,
  o da bossa index 0 = profilsiz).
- Test: `tests/run_props_profile_panel.py` (ctest `model_maker_props_profile_panel`):
  sekil (ribbon cubugu gitmis / Props tek kaynak / ENTER yollari / clear yok) +
  gercek `comboProfileName` govdesi VERBATIM cikarilip sahte QString/QVariant/QComboBox
  sinirinda derlenir (yer tutucu -> '', veri -> ad, elle yazilan -> kirpilmis metin).
  Mutasyon: `tests/mutate_props_profile_panel.py` (9 mutasyon, 9'u yakalanir; dosyayi
  bayt-birebir geri yazar). Tuzak: `&QLineEdit::returnPressed` icin GENEL kontrol
  YETMEZ — malzeme satirinda da aynisi var, kontrol profil satirina ozel olmali.

---

## Profil katalogu / listeleme sirasi

- Kaynak: `ensureProfileCatalog()` once Tekla `.lis` klasorlerini dener
  (`C:\TeklaStructures\2025.0|2023.0\Environments\...\Profil`), sonra exe yanındaki
  `profiles/european.lis` (gomulu Avrupa katalogu, 123 kayit: HEA/HEB/HEM/IPE/IPN/UPN).
- `loadProfileCatalog` `std::filesystem::directory_iterator` kullanir -> **sira tanimsiz**;
  katalog `profileNameLess` (dogal/sayisal karsilastirici) ile siralanir.
- `Application::profileNames()` (Qt dropdown'i besler) ayni `profileNameLess` ile siralar.
  DUZ `std::sort` KULLANMA: sozluk sirasi HEA1000'i HEA120'den once, IPE80'i en sona koyar.
- **v6: Qt tarafinda TEK profil UI'si Props panelidir** (bkz. asagidaki "Profil secimi
  Props panelinde" bolumu). Eski ribbon `profileBar` + `categoryOf` kategori basliklari
  KALDIRILDI; `profileNames()`'i artik yalniz `createDockPanels` cagirir. Kategori
  basligi/gruplama geri eklenirse Props combo'su da o sekle uymali. Win32 combo
  (`refreshProfileCombo`) dogrudan `profileCatalog_` sirasini kullanir (dogal).
- Not: "IPN" (Avrupa I-profil) icin ayri kontrol gerekir; sadece "INP" kontrol edilirse IPN
  "Diger" kovasina dusup listenin en basinda gorunur.

---

## Parametre giris (edit box)

- Tum parametreli komutlar sayisal degeri Qt `QInputDialog::getText` (edit box) ile ister:
  `Application::setParameterRequest` callback'i Qt tarafinda kurulur; `requestDoubleParameter`/
  `requestCountParameter` prompt ile sorar, `std::stod/stol` ile parse eder (2..1000 count, >0 double).
- Komut basinda istenenler: **Offset mesafesi, Fillet yaricapi, Dizi sayisi (linear/polar)** —
  `startTransformCommand` icinde; iptal = komut baslamaz. **Divide sayisi** `activateDivide()` icinde
  ('D' ve menu burayi cagirir; imlec-yani rakam yazma kaldirildi).
- Offset mesafe onceden girilince `Selecting`/`BasePoint` yerine dogrudan `Destination`'a gecilir
  (phase mantiginda `Offset && offsetDistance_` dali).

- **Point objeleri gorunmez bug**: `WireframeModel::point` tek tepe + sifir kenar; GDI `drawModel`
  sadece kenar cizer, GL line batch'i kenarsiz modeli atlar. Bu yuzden Divide/DXF point objeleri
  gorunmuyordu. Cozum: renderer'da GL kompozitinden SONRA (her iki modda da) ozel bir pass
  ekran-uzayi X isareti cizer (`renderer.cpp` ~1234: `isPointEntity()` filtre, `createEntityPen` +
  4px X cizgi). Yeni nokta-iceren ozellik eklerken bu pass'i goz onunde tut.
