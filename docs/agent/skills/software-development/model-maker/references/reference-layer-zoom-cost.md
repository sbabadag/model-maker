# Referans katmani (grid akslari + UCS glifi) DERIN ZOOM maliyeti

KULLANICI SIKAYETI: "zoom in arttiginda zoom cok yavasliyor bunu hizlandiralim
olduca". Bir onceki tur kontur/siluet pass'ini cozmustu; bu KALAN maliyettir.

## Kok neden (OLCULDU, tahmin degil)

Sabit referans katmani DUNYA biriminde tanimli geometridir ve dogrudan
projeksiyonlanir. Kirpma/olcek kurali olmadan zoom arttikca AYNI cizgi EKRANDA
dogrusal buyur. Tekerlek animasyonu (timer 8) ~15 ms'de bir kare ister ve
`wheelNavigating_` nedeniyle hareket hizli yolu KAPALIDIR → her kare tam redraw;
kare maliyeti cizgi uzunluguyla buyudugu icin zoom derinlestikce yavaslar.

Olcum (`tests/zoom_deep_cost_probe.cpp`, gercek Camera + grid, 1600x900 ISO plan
gridi, 48 detent = 1.12^48 = 231x):

| | 0 detent | 48 detent |
|---|---|---|
| toplam cizilen ekran uzunlugu | 8.091 px | **1.864.015 px** |
| tek en uzun aks | 1.093 px | **251.894 px** |
| UCS oku ekranda | 120 px | **758 px** |

Mekanizma: akslar `PS_GEOMETRIC|PS_USERSTYLE` KESIKLI kalemle cizilir; GDI
geometric pen desen fazini KIRPMA ONCESINDE TUM hat boyunca hesaplar → maliyet
uzunlukla buyur. (Model/tel kafes cizgileri de zoom'da uzar ama culling ile
sinirlidir; bu yuzden once referans katmanina bak.)

## KURALLAR — tek kaynak `include/model_maker/reference_layer.hpp` (portatif)

1. **Referans segmentleri EKRAN UZAYINDA kirpilir.** `clipSegmentToRect`
   (Liang-Barsky), dikdortgen = tuval + 64 px PAY (etiket/ofset kenarda
   kesilmesin). Cizgi <= tuval kosegeni olur, maliyet zoom'dan BAGIMSIZ kalir.
   Olcum: derin zoom'da ham 1.864.014 px -> cizilen **2.755 px** (~677x).
2. **Etiket capasi KIRPILMAMIS uctan** hesaplanir (konumu zoom'dan bagimsiz
   sabit). Cizim yalniz kirpilmis segment icin; 0.5 px altindaki segment atlanir.
3. **Glif EKRAN-OLCEKLI olmali**: `referenceGlyphWorldLength(pxPerUnit, 120)`.
   Dunya biriminde ALT SINIR (eski `clamp(120/glyphPx, 60, 60000)`) ekran
   uzunlugunu `60*pxPerUnit`'e sisirir ve zoom ile dogrusal buyur. Tek dogru
   form: dunya uzunlugu = hedefPx / pxPerUnit.
4. **GDI nesnesi DONGU DISINDA.** Kesikli kalem eskiden HER aks icin
   `ExtCreatePen`+`DeleteObject` ile yaratiliyordu (kare basina 8 nesne).
5. Yeni bir referans-katmani ogesi eklerken AYNI sekli kullan: kirp +
   ekran-olcekli tut.

## Testler

- `model_maker_reference_layer_test` (portatif, OCC'siz): kirpma geometrisi,
  glif "her px/unit'te ekranda 120 px" invaryanti, ve GERCEK Camera + gercek
  `gridAxesForView` ile derin zoom taramasi — cizilen toplam sinirli, ham toplam
  kat kat buyuk.
- `model_maker_zoom_reference_clip` + `tests/zoom_reference_clip_cases.cpp`:
  uretim grid blogu VERBATIM + GERCEK `mm::Camera` + kaydedici sahte GDI.
  Invaryantlar: cizilen toplam sinirli (0..48 detent), gorunen aks DUSMEZ
  (`drawn >= visible`, gorunurluk uretim kirpmasindan BAGIMSIZ ornekleme ile),
  pay bandi disinda cizim yok (`drawn <= marginAxes`).
- `model_maker_zoom_deep_cost_probe`: olcumu tekrarlanabilir kilar (iddia yok).
- Mutasyon: `tests/mutate_zoom_reference_clip.py` — 4/4 yakalar (kirpma kaldir /
  kirpma dikdortgeni dev / glif kelepcesini geri koy / kalemi donguye tasi).

## Harness tuzaklari (ogrenildi)

- **Sahte `projectPoint` olcegi TUVALE SIGMALI** (ornegin 0.06 px/mm). 65 px/mm
  ile grid ekranin yuz binlerce piksel disina duser; kirpma devreye girince HIC
  aks cizilmez ve aile-sayimi testleri bosuna duser. Yon deseni AYRI, olcek
  AYRI: `axisOf` normalize ettigi icin `kScreen*` yalnizca YON tasir.
- **Kirpma dikdortgeni tuvale ESIT DEGIL** (tuval + pay). Bu yuzden
  "hicbir gorunen aks dusmesin" invaryanti `drawn >= visible` (kati tuval),
  "pay disinda cizim olmasin" invaryanti `drawn <= marginAxes`.
- Test dosyasi global scope'ta oldugu icin uretim kodu (`namespace mm` icinde)
  niteliksiz cagirir → harness'a `using mm::clipSegmentToRect;` ekle.
- Uretim blogu bir harness tarafindan METIN SINIRLARIYLA cikariliyorsa, bloga
  eklenen yeni kod eski bitis isaretcisinin icine girer; isaretcileri kontrol et
  (bkz. SKILL.md harness maddeleri).

---

<!-- SKILL.md'den taşındı 2026-10-19 (gövde sadeleştirme) -->

## Sabit referans katmani (grid/UCS/hayalet duzlem) FLICKER — ekran-olcegi kurali

KULLANICI SIKAYETI: "sabit cizilenler (work plane, UCS icon) zoom'da ve pan'da cok
flicker yapiyor". Kok neden OLCULDU (tahmin degil):

- **KURAL: ekran-olcekli bir nicelik turetirken YUVARLANMAMIS projeksiyon kullan.**
  `renderer.cpp`'de `projectPoint` POINT'e (tamsayi) oturtur. Iki noktanin TAMSAYI
  farkini bolerek olcek turetmek (`120/glyphPx`, `0.30*span/ghostPx`) zoom'da boyutu
  kare kare oynatir. TEK KAYNAK: `projectPointPrecise` (Vec2, double) — UCS glif ve
  hayalet duzlem olcegi BUNDAN beslenir; POINT'e oturtulmus surum yalnizca CIZIM icin.
- Olcum (`tests/reference_layer_scale_probe.cpp`, gercek Camera + build-hermes
  `libmodel_maker_core.a`): saf zoom taramasinda (240 kare) hayalet duzlem yarim-kenari
  TAMSAYI girdiyle 180.00..182.49 px arasinda gidip geliyor ve 240 karenin **182'sinde
  YON DEGISTIRIYOR**; yuvarlanmamis projeksiyonla sapma **0.000 px**. Ayni probe'u yeni
  bir ekran-olcekli katman eklerken tekrar kullan.
- **Kesme -> lround:** `projectPoint` eskiden `static_cast<LONG>` (TRUNCATION) idi; kesme
  hatayi hep sol-yukari yigar, sabit katman alt-piksel hareket eden GL modeline karsi
  kayar. `std::lround` hatayi [-0.5,+0.5] yapar. NOT: lround olcek salinimini GIDERMEZ
  (olcum: yine 183 yon degisimi) — ikisi AYRI duzeltmedir.
- **Desen (hatch) firca kaynagi:** `CreateHatchBrush` cihaz kaynagina cakilir; varsayilan
  (0,0) iken duzlem pan/zoom'da kayinca doku duzlemin ALTINDAN akar (crawling).
  `SetBrushOrgEx(dc, phase(origin.x), phase(origin.y), &prev)` ile duzlem orijinine
  faz-kilitlenir (8x8 periyot; negatif modulo normalize: `v<0 ? v+8 : v`), sonra GERI
  ALINIR (DC durumu paylasimli).
- **Motion fast path navigasyon sirasinda KAPALI olmali:** hizli yol kamera parmak izini
  4 noktayi TAMSAYI karsilastirarak kurar; yavas/yaklasan harekette kamera degistigi halde
  iz eslesir ve BAYAT taban basilir -> sabit katman bir kare eski konumda gorunup ziplar.
  Kosula `!draft.panning && !draft.rotating && !draft.wheelNavigating && !draft.viewCubeActive`
  EKLE (`draft.interactiveNavigation` KULLANMA — o `snapPreviewActive_` icerdigi icin hizli
  yolun kendi kosulunu hep öldürür).
- `paintPassiveViewport` bayat navigasyon bayraklarini da temizler (`view.panning/rotating/
  wheelNavigating/viewCubeActive = false`) — aksi halde pasif pencere bunlari miras alir.
- Test: `tests/run_workplane_ghost.py` + `tests/workplane_ghost_cases.cpp` (artik 8 davranis
  testi). Yeni testler: (a) zoom taramasinda duzlemin EKRAN boyutu sabit (<=3 px sapma);
  (b) desen fazi duzlemle hareket eder (tam 8 px periyot fazi degistirmez, kismi adim
  degistirir). Mutasyonla dogrulandi: yuvarlanmis turetme / SetBrushOrgEx kaldirma /
  truncation'a donus / navigasyon kapisi kaldirma -> dordu de testi dusurur.
- **Harness tuzagi (olculdu):** alt-piksel kamerayi SAF OTELEME olarak modellemek
  regresyonu YAKALAMAZ — tamsayi koordinatlarda ayni offset iki noktayi birlikte kaydirir
  ve fark degismez. Gercek zoom OLCEGI degistirir: harness'a surekli degisen bir `g_zoom`
  carpani koy (1.003..1.027 gibi) ve onu iki projeksiyonda da kullan. Ayrica `-Werror`
  altinda mutasyon yaparken kullanilmayan degisken birakma — derleme patlar ve davranis
  testi HIC kosmaz (yanlislikla "yakalanmadi" sanilir).

---

## GL yolunda KARE MALIYETI: kontur pass'i ("zoom yavasliyor" sikayeti)

KULLANICI SIKAYETI: "zoom derinlestikce cizim cok yavasliyor, geri zoom da yavas".
Kok neden OLCULDU: GL Solid yolunda `renderContourLines` HER KAREDE tum modelleri
bastan isliyordu. 18k profil katisi (648k kenar / 252k yuz) icin **475 ms/kare** →
tekerlek zoom'u ~2 FPS. Uc ayri israf vardi: (1) model basina iki
`std::map<pair<size_t,size_t>,int>` (yuz binlerce dugum), (2) ~1M segmentlik ara
vektorun her karede doldurulup IKINCI dongude batch'e kopyalanmasi (~56 MB/kare),
(3) kare basina GPU buffer yarat/yik (`glDeleteVertexArrays` fonksiyon SONUNDA).

KURALLAR:
- **Kontur/siluet pass'i ONBELLEKLENIR.** TEK KAYNAK `mm::buildContourBatch`
  (`include/model_maker/contour_builder.hpp`, PORTATIF — Windows basligi tanimaz,
  Linux'ta test edilir). Cagiran kendi batch vektorlerini verir; ara kopya YOK.
- **Onbellek anahtari: icerik surumu + model/kenar sayisi + tuval olcusu + 2B/3B +
  `Camera::viewTransform` tabani (9 double).** Zoom ve pan bu tabani
  DEGISTIRMEZ; izdusum ucgeninin 2B alan ISARETI duzgun olcek + oteleme altinda
  korunur → zoom/pan karelerinde pass hic calismaz. `width/height` anahtarda
  olmali (projeksiyon onlara bagli) ve resize'da gecersiz kilar.
- **GPU buffer'lari fonksiyon sonunda SILME.** Onbellekli batch kareler arasi
  yasar; serbest birakma `cleanupGL()`'de + baglam kaybinda, ve orada
  `contourCacheValid_ = false` yap (yoksa bayat buffer'a cizilir).
- Model basina `std::map` yerine `ContourWorkspace`: TEK acik-adresli hash,
  model basina yalniz `beginModel()` = `++generation_` (temizleme maliyeti YOK).
  Yeni bir "turetilmis nokta/segment turu" eklerken ayni sekli kullan.
- OLU KOD UYARISI: `Application::wheelPreviewFactor_` yalnizca 1.0'a atanir
  (hicbir yerde gercek carpan yazilmaz) → `view.rasterZoomPreview` HER ZAMAN
  false, yani raster zoom onizlemesi olu. Zoom performansi icin ona guvenme.
- Mekansal sorgu (`queryBounds` + `projectedBoundsIntersectsViewport`) suclu
  DEGIL: `model_maker_render_prep_benchmark` 100k varlikta
  `projected_3d_query_ms=0.13` veriyor. Once olc, sonra suclu ilan et.

Testler: `model_maker_contour_builder_test` (referans `std::map` uygulamasiyla
4 gorunuste esdegerlik, determinizm, workspace VE CIKTI TAMPONU yeniden kullanimi,
oryantasyon duyarliligi, yuzsuz model davranisi) + `model_maker_zoom_cost_probe`
(esdegerlik + onbellek anahtari gecerliligi; zamanlama yalniz RAPORLANIR).
Mutasyonlar (M1 `beginModel` etkisiz, M2 siluet yok, M3 `clear()` kaldirildi) —
uCU de yakalanir. Tuzak: test cikti vektorlerini her cagrida TAZE olusturursa
"clear kaldirildi" mutasyonu KACAR; gercek arka uc ayni vektoru yeniden kullandigi
icin test de ayni tamponu tekrar kullanmali. Olcum icin duzgun kullanim:
`./build-hermes/model_maker_zoom_cost_probe 18000`.

---

## Referans katmani (grid akslari + UCS glifi) DERIN ZOOM maliyeti

KULLANICI: "zoom in arttiginda zoom cok yavasliyor bunu hizlandiralim oldukca".
Sabit referans katmani DUNYA biriminde tanimlidir; kirpma/olcek kurali olmadan
zoom arttikca cizgi EKRANDA dogrusal buyur (OLCUM: toplam 8.091 -> 1.864.015 px
@231x; UCS oku 120 -> 758 px; akslar PS_GEOMETRIC kesikli kalemle cizilir ve GDI
desen fazini tum hat boyunca hesaplar).

KURAL (tek kaynak `include/model_maker/reference_layer.hpp`, portatif):
referans segmentleri EKRAN UZAYINDA tuval+64px'e kirpilir
(`clipSegmentToRect`); UCS glifi EKRAN-OLCEKLI tutulur
(`referenceGlyphWorldLength`; dunya-birimi alt sinir YASAK — zoom ile buyur).
Sonuc: derin zoom'da ham 1.864.014 px -> cizilen 2.755 px (~677x).

Tam olcum tablosu, kurallar, harness tuzaklari ve mutasyon listesi:
`references/reference-layer-zoom-cost.md`. Testler: `model_maker_reference_layer_test`,
`model_maker_zoom_reference_clip`, `model_maker_zoom_deep_cost_probe`;
mutasyon `mutate_zoom_reference_clip.py` (4/4).

---

## Calisma duzlemi hayaleti (ghost plane) — her pencerede

Aktif is duzlemi (UCS) HER viewport'ta yari saydam bir duzlem yamasi olarak cizilir.
Yalnizca cizimdir: **secilemez, snap'lenemez**, hicbir belge nesnesi uretmez.

- **Nereye koyulur:** `renderer.cpp`'de `drawGridAndAxes` lambda'sinin ICINE (blok sonunda,
  `}; // drawGridAndAxes` kapanisindan hemen once). Bu lambda hem GDI yolunda modelden ONCE
  (`if (!useGpuLines) drawGridAndAxes(dc);`) hem GL kompozit yolunda modelin USTUNE
  (`drawGridAndAxes(dc);`) cagrilir — yani **her pencereye otomatik** girer. Yeni bir
  "her pencerede gorunsun" katmani icin ayri pass YAZMA; buraya ekle.
- **Kapı:** `DraftView.workPlaneGhostVisible`. `Application::draftView()` bunu
  `workPlaneGhostVisible_`'den tasir; pasif viewport da ayni `draftView()`'dan gectigi
  icin ayrica is yok.
- **Varsayilan KAPALI (kullanici istegi, "simdilik disable").** Tek kaynak:
  `kWorkPlaneGhostDefaultVisible` (`include/model_maker/renderer.hpp`, su an `false`).
  HEM `DraftView` alani HEM `Application::workPlaneGhostVisible_` bu sabitten turer —
  **ikinci bir `{true}` literal YAZMA**: iki varsayilan ayrisirsa hayalet kendiliginden
  geri gelir. Geri acmak: sabiti `true` yap, ya da calisma aninda F7 / Gorunum menusu
  (toggle yerinde duruyor). Hicbir ayar dosyasi bu bayragi kalici tutmaz (QSettings/ini yok),
  o yuzden sabit tek gercek kaynak.
- **Olcek:** EKRAN-OLCEKLI — `ghostHalf = 0.30 * min(canvas en/boy) / ghostPixelPerUnit`
  (projeksiyondan olculur, ayni eksen glifinin `120.0/glyphPx` mantigi). Zoom'da gorunur
  kalir, model boyutunu degistirmez. Kenardan bakista dortgen coker ama cizim devam eder.
- **Gorsel:** dolgu `CreateHatchBrush(HS_FDIAGONAL, RGB(126,156,186))` + `SetBkMode(TRANSPARENT)`
  → ekran-kapi (screen-door) yari saydamlik; GDI'da keyfi dortgen icin AlphaBlend/maske
  GEREKMEZ. Ic mesh 6x6, 1px `RGB(146,176,204)`; cerceve 2px `RGB(74,110,152)`.
  Kontrast (soluk fon RGB(198,224,246)): cerceve 3.9:1, dolgu 2.1:1, mesh 1.65:1.
  Firca kaynagi duzlem orijinine faz-kilitlenir (bkz. yukaridaki FLICKER bolumu).
- **Toggle tek kaynak:** `Application::toggleWorkPlaneGhost()` — F7 (canvas proc WM_KEYDOWN
  + dis pencere proc WM_KEYDOWN) ve Gorunum menusu (isaretlenebilir QAction) ayni yeri cagirir.
  F7 sistem tusu DEGIL, `WM_KEYDOWN` dogru yer (F10'un tersi).
- **Test:** `python3 tests/run_workplane_ghost.py build` (ctest `model_maker_workplane_ghost`).
  Iki katman: (1) kaynak-sekli — blok `drawGridAndAxes` icinde, `drawGridAndAxes(dc)` iki
  yolda da var, blokta `document`/`selectedModels`/`snapType`/`hover_` YOK ve `draft.* =`
  ATAMASI yok (render-only = secilemez/snap'lenemez kaniti), F7 iki proc'ta; (2) uretim blogu
  VERBATIM cikarilip sahte kaydedici GDI'ya karsi derlenir (`gumball_render_cases.cpp` sekli):
  dolgu/mesh/cerceve sayilari, kapali cerceve, yerel cerceveye hizalama, ekran-olcek, renk
  kontrasti, kenardan bakis. 3 mutasyonla dogrulandi (world-eksen, opak dolgu, sabit olcek).
- **Varsayilan-kapali testleri:** ayni betik artik ayrica (a) sabitin `false` oldugunu,
  (b) `DraftView` alaninin ve (c) `Application` bayraginin AYNI sabitten turedigini, ve
  (d) hicbir eski `{true}` literalinin kalmadigini kontrol eder. Mutasyon kosucusu:
  `python3 tests/mutate_workplane_ghost_default.py build`
  (4 mutasyon: kill-switch true / DraftView literal true / Application literal true /
  tek kaynak kaldirildi). **Genel kural:** bir ozelligin varsayilanini kapatan/acan bayrak
  iki yerde literal yazilmaz — tek adlandirilmis sabit (`k...Default...`) ve testi "eski
  literal kalmadi" kontrolu icerir, yoksa varsayilanlar zamanla ayrisir.
- **Harness tuzaklari (ogrenildi):** (a) `stockPen`/`stockBrush` sahte sinirda GERCEK koddaki
  gibi `HGDIOBJ` (pointer) olmali — `Object` degeri olursa `SelectObject(dc, stockPen)`
  derlenmez; (b) `projectPoint` tamsayiya yuvarlandigi icin YON karsilastirmasinda 1 birim
  taban (~28 px) gurultuye bogulur → **uzun taban** kullan (`fromPlane({+-10000,0})`),
  esik ~0.01 (regresyon sinyali ~0.85); (c) `CreateHatchBrush`/`SetBkMode`/`GetStockObject(NULL_PEN)`
  stub'larini ekle.

---

## Track / guide renkleri (fona zit)

- Tek kaynak `include/model_maker/color_contrast.hpp`: tuval gradient uclari
  (`kCanvasGradientTopRgb/BottomRgb`, renderer GradientFill buradan okur) + `trackGuideRgb()`
  = fonun tamamlayici tonu, iki uca karsi WCAG >=4.5 olana dek koyulastirilir (su an (153,66,0)).
  Ortho eksen guide'lari `kGuideAxisX/Y/ZRgb` (koyu kirmizi/yesil/mavi).
- renderer.cpp'de `trackGuideColor()` / `guideAxisColor(OrthoAxis)` kullan; track/guide icin
  sabit RGB yazma (eski cyan/sari acik fonda kontrast ~1.1 idi). Fon degisirse yalniz sabitleri degistir.
- Test: `model_maker_color_contrast_test` (ctest, Linux).
