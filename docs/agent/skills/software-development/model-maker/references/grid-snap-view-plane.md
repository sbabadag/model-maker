# Grid snap görünüş düzlemi (XZ/YZ iki noktalı görünüş)

KULLANICI ŞARTI (iki aşamalı):
1. "ayrica grid e tum snaplar yapismali" → aks çizgileri uç/orta/en yakın/dikme adayı üretir.
2. "XZ YZ gibi 2 noktayla alinan goruntulerde grid sadece xy deki alt grid noktalarina snap
   oluyor; her grid snap ozelligine kilitlenmeli".

## Kök neden (ölçüldü — gerçek SnapEngine + Camera + grid)

Snap adayları `grid.axes` üzerinden üretiliyordu. `grid.axes` = `rebuildGridAxes` türevi = PLAN
geometrisi: tüm plan çizgileri `z = origin.z`'de, kat çizgileri yalnız ön/arka kenarda. ÇİZİM ise
`gridAxesForView(grid, plane)` (görünüşe göre) kullanıyordu. Sonuç: XZ/YZ elevasyonda snap yalnızca
taban kotundeki XY noktalarına kilitleniyordu.

Probe çıktısı (tests/grid_snap_view_plane_probe.cpp) — DÜZELTMEDEN ÖNCE:
```
XZ elevasyon (gridViewDirection 0,-1,0)
(a) hedef (0,0,2000)      -> tip=Grid  nokta=(0,0,0)      <-- TABANA çekiyor
(b) hedef (4000,0,3600)   -> tip=Nearest nokta=(4000,-1000,3600)  <-- taban kenarı
YZ elevasyon
(c) hedef (0,0,5000)      -> tip=Grid  nokta=(0,0,0)      <-- TABANA çekiyor
```
DÜZELTMEDEN SONRA: (a) Nearest (0,0,2000), (b) Nearest (4000,0,3600), (c) Nearest (0,0,5000).

## Düzeltme — TEK KAYNAK

- `addGridLineCandidates(..., const ScreenSpaceNearest* screenSpace = nullptr,
  GridViewPlane plane = GridViewPlane::XY)` — artık `gridAxesForView(grid, plane)` kullanır;
  yani **snap geometrisi = çizim geometrisi** (aynı fonksiyon).
- `snap3D`: `const GridViewPlane gridPlane = gridViewPlaneForDirection(camera.gridViewDirection());`
  (KİLİTLİ yön; canlı `R_` değil) ve bu düzlem çağrıya geçirilir. `snap()` (2B, Draw2D) plan (XY)
  kalır — imzasında kamera yok.
- 3B toleranssız kesişim fallback bloğu da `gridAxesForView(grid, gridPlane)` + `lineCrossing`.
- `horizontal` bayrağı üç düzlemde de tutarlı: `false` = düzlem-içi DİKEY aile, `true` = düzlem-içi
  YATAY (kat) aile. Eşleştirme `verticalAxes × levelAxes`.

## Kesim formülü genelleştirildi (`lineCrossing`)

Eski formül `Vec3{xa->from.x, ya->from.y, xa->from.z}` **YALNIZ plan için** doğruydu; elevasyonda
z bileşenini X-ailesinin BAŞINDAN (= zBottom) alıyordu → kavşaklar taban kotunda oluşuyordu.
Artık iki doğru parçasının gerçek kesişmesi çözülür:
```
Vec3 cross3(u,v) -> u x v
n = cross3(dA,dB); n2 = |n|^2; if (n2 <= epsilon) false;   // paralel/dejenere
t = dot(cross3(B.from-A.from, dB), n) / n2;   if (t<0||t>1) false;
out = A.from + dA*t;
s = dot(out-B.from, dB) / |dB|^2;   return s in [-1e-9, 1+1e-9];
```
DİKKAT (işaret tuzağı): `s`'yi cross çarpımıyla hesaplarken işaret TERS çıkar (`dB×dA = -n`).
Projeksiyonla (`dot(out-B.from,dB)/|dB|^2`) hesaplamak bu tuzağı tamamen atlar.

## Testler ve mutasyon

- `tests/grid_snap_types_test.cpp` (ctest `model_maker_grid_snap_types_test`): 20 → **32 kontrol**.
  Yeni: XZ/YZ dikey aks kotu, kat çizgisi, kavşak kotu (15: (12000,0,7200) hiçbir orta noktayla
  çakışmaz → saf `SnapType::Grid`), maske kapısı, taban (z=0) hâlâ erişilebilir.
- **KATLI grid şart:** `makeGrid()` zCoords içermez → XZ/YZ'de düzlem-içi YATAY aile (kat) BOŞ kalır,
  kavşak hiç oluşmaz ve snap `None` döner (imleç ham noktada kalır — yanıltıcı "PASS").
  Elevasyon testleri ayrı `makeLevelGrid()` (zCoords {0,3600,7200}) kullanmalı.
- `tests/mutate_grid_snap_types.py`: 9 → **13 mutasyon, 13/13 yakalar**. Yeniler:
  M10 snap taban plan türevine döner (KULLANICI HATASI regresyonu), M11 düzlem daima XY,
  M12 kesişim eski plan formülüne döner, M13 3B fallback taban türevine döner.
- **Mutasyon deseni tekilliği:** `if (!lineCrossing(*va,*la,intersection)) continue;` tek başına
  2 yerde eşleşir (girinti iç içe geçer — 20 boşluklu satır 16 boşluklu deseni İÇERİR). Önceki
  `Vec3 intersection{};` satırını desene ekle (o zaman `\n` + tam 16 boşluk gerekir ve 20 boşluklu
  satır eşleşmez).
- ctest **36/36**. Probe CMake'e kayıtlı: `model_maker_grid_snap_view_plane_probe`.

## Dokunulmayanlar (bilerek)

- `grid.axes` hâlâ `Document::bounds` (zoom-extents) ve 2B snap fallback'i tarafından kullanılır;
  semantiği DEĞİŞMEDİ. Yalnız 3B snap geometrisi görünüşe bağlandı.
- Görünüş düzlemi KİLİTLİ yönden gelir; orta tuş rotasyonu grid çizimini/snap'ini değiştirmez
  (kullanıcı şartı: "grid ... bir kez tek düzlemde çizilir").

## Renderer harness tuzakları (GDI sahte sınırı — yaşandı)

Görünüş-düzlemi çizim/snap testleri `src/grid.cpp`'yi + ÜRETİM bloğunu sahte kaydedici GDI
sınırında derler. Bu tuzaplar "davranış testi koşmadı" yanılsaması yaratır:

- (a) Blok `gridViewPlaneForDirection`/`GridViewPlane`/`gridAxesForView`/`GridAxisLine`'ı
  NİTELİKSİZ çağırır → sahte TUS'a `using mm::GridViewPlane; using mm::gridAxesForView;
  using mm::gridViewPlaneForDirection; using mm::GridAxisLine;` ekle.
- (b) `CreateFontW` 13 int + LPCWSTR = 14 argüman; eksik parametre "too many arguments".
  `ExtCreatePen`'in son parametresi `const DWORD*` (DWORD = unsigned long), `unsigned*` DEĞİL.
- (c) Sahte DC'de VARSAYILAN FONT seçili olmalı (stok font): `HGDIOBJ oldFont =
  SelectObject(dc, labelFont)` null dönerse renderer eski fontu GERİ SEÇMEZ ve `DeleteObject`
  seçili nesneyi siler — "font restored" iddiası yanlış nedenle düşer.
- (d) `FALSE`, `DEFAULT_CHARSET`, `OUT_TT_PRECIS`, `CLIP_DEFAULT_PRECIS`, `CLEARTYPE_QUALITY`,
  `DEFAULT_PITCH`, `LOGBRUSH`, `PS_GEOMETRIC/PS_USERSTYLE/PS_ENDCAP_FLAT`, `BS_SOLID` stub'ları
  gerekir.

---

<!-- SKILL.md'den taşındı 2026-10-19 (gövde sadeleştirme) -->

## TEKLA-TARZI YAPI GRIDI (v7) — grid bir SPESIFIKASYONDUR

KULLANICI SARTI: "grid objemizi birebir Tekla'nin calisma sistemiyle ayni
ayarlayalim" (Tekla Structures 2026: Work with grids / Create a grid /
Grid properties). Tekla'da grid OBJECT degil, bir OZELLIK SPESIFIKASYONUDUR;
cizim geometrisi ONDAN turetilir.

- **PORTATIF CEKIRDEK: `include/model_maker/grid.hpp` + `src/grid.cpp`.**
  `GridAxisLine`/`GridDefinition` ARTIK BURADA (document.hpp'den tasindi;
document.hpp `#include "model_maker/grid.hpp"` yapar). API:
  `parseGridCoordinateLine(text, cumulative)` ("N*deger" tekrari; X/Y
  cumulative=true, Z false), `parseGridLabelLine`, `gridAutoNumberLabel`
  (1,2,3..), `gridAutoLetterLabel` (A..Z, AA), `ensureGridLabels`,
  `rebuildGridAxes`. **`axes` TUREVdir** — elle doldurma; `rebuildGridAxes`
  veya `Document::setGrid` (o da cagirir) kullan.
- **Tekla semantigi (dokumandan birebir):** X koordinatlari Y'ye PARALEL
  dikey cizgileri, Y koordinatlari X'e paralel yatay cizgileri konumlar;
  X kutusu etiketleri Y'ye paralel cizgileri, Y kutusu X'e paralel
  cizgileri adlandirir (dokuman: "Names in the X box are associated with
  the grid lines parallel to the y axis and vice versa"); Z kotlari MUTLAK,
  X/Y ARTIMSAL. **Uzanti: Left/Below ve Right/Above AYRI** (eski kodda
  sabit %20 idi). Origin is duzleminden offset'tir, global degil.
  Etiket kutusu TAMAMEN bos birakilabilir (o zaman etiketsiz cizilir) —
  `ensureGridLabels` YALNIZ dolu ama kisa listeyi tamamlar; bos listeye
  DOKUNMAZ (M6 mutasyonu bunu kilitler).
- **Aks etiket KONVANSIYONU (kullanici sarti, Tekla varsayilaninin TERSI):**
  X ailesi (Y'ye paralel DIKEY cizgiler) HARF alir, **DUZ SIRA** (en kucuk
  X = A, saga dogru B, C, ...); Y ailesi (X'e paralel YATAY cizgiler) SAYI
  alir, "asagidan yukari" (en kucuk Y = 1). `ensureGridLabels`: X icin
  `gridAutoLetterLabel(i)` (duz sira), Y icin `gridAutoNumberLabel(i)`.
  `promptCreateGrid` varsayilani da aynisini uretir. Tekla X'e sayi / Y'ye
  harf koyar; kullanici bunu tersine cevirdi.
  (ONCEKI DENEME — "sagdan sola": X icin `gridAutoLetterLabel(n-1-i)` idi;
  kullanici "2" secenegiyle DUZ SIRAYA cevirdi, boylece **A/1 kesisimi
  pozitif X/Y ile orijinde kalir** (once sag-alt kosede kaliyordu). Negatif
  X kurmak gerekmez. Geri donmek isterse tek satir: `n-1-i`.)
- **KALICILIK: MMW7.** Grid blogu spesifikasyonu yazar; MMW5/6 dosyalari
  AYNEN okunur (yalniz-axes geometri, `xCoords` bos → eskisi gibi cizilir
  ve snap'lenir ama spec dialog'unda duzenlenemez). Yeni bir grid alani
  eklerken: alan + yaz + oku + `version7` kapisi + eski bicim dali.
- **UNDO:** `UndoOp::Kind::{GridAdd,GridRemove,GridReplace}` + `gridIndex`
  /`gridBefore`/`gridAfter`. **`Document::addGrid/setGrid/removeGrid` KENDI
  op'unu kaydeder** (addModel/moveModels gibi). Cagiran YALNIZ
  `pushUndoSnapshot()` der; op'u ikinci kez kaydetme → cift geri alma adimi.
  OLCULEN ESKI KUSUR: eski `addGrid` op kaydetmiyordu → grid ekleme geri
  ALINAMIYORDU (sessiz).
- **Qt (tek UI):** "Yapi Gridi Olustur..." → `promptCreateGrid()` +
  `editGridProperties(grid, title)` (Tekla property pane karsiligi: Name,
  Coordinates X/Y/Z, Labels X/Y/Z, Line extensions Left/Below-Right/Above,
  Origin X/Y/Z, Magnetic grid plane, Grid color, Label font size/color,
  gorunurluk). "Yapi Gridleri..." → `promptGridManager()` (liste + Yeni/
  Duzenle/Sil/Gorunurluk; **satira cift tik = duzenle** — Tekla'da grid
  cizgisine cift tiklamanin karsiligi). Koordinat kutulari grid degerini
  ARTIMSAL bicimde geri yazar (round-trip kararli).
- **Renderer:** grid rengini/yazi boyunu SPESIFIKASYONDAN okur (`grid.color`,
  `grid.labelColor`, `grid.labelFontSize`; CreateFontW + SetBkMode/geri
  alma). Eski `GRIDDRAW` tanilama log'u KALDIRILDI (log dosyasi kirliligi).
- **HENUZ YOK (dürüst liste):** `magnetic` alani saklanir ama MAGNETIC
  BINDING (nesnelerin aksa baglanip aks cizgisi hareket edince
  izlemesi) YOK; RADIAL grid yok (`GridKind::Radial` yer tutucu); tek
  tek grid cizgisi ekleme/tutamakla egme yok; grid secme (3D/2D pick)
  ve "grid cizgisine cift tik" pick tabanli akis yok.
- Testler: `model_maker_grid_test` (`tests/grid_test.cpp`, **65 kontrol**,
  OCC'siz/Linux'ta kosar) + `tests/mutate_grid.py` (8 mutasyon: kumulatif
  parse, "N*deger", uzanti ayrimi, X/Y etiket eslesmesi, origin offset,
  bos-etiket kurali, grid undo, spec kaydi → **8/8 yakalanir**, dosyalari
  bayt-birebir geri yazar). ctest 27/27.

---

## GRID GORUNUS DUZLEMI — "3B grid gozukmez" (v8)

KULLANICI SARTI: "grid de pencerenin duzlemine gore sadece xy yz xz gridleri
gozukur 3d grid gozukmez". TEKLA KURALI (dokumandan birebir): "A grid represents
a three-dimensional complex of horizontal and vertical planes. The grid is
shown on the view plane using dash-and-dot lines" + "The grid is only shown in
the view if any of the grids' planes intersect with the view plane." Yani
**gorunus duzlemine PARALEL aile cizilmez**: planda kat duzlemleri, kesitte
derinlik duzlemleri gorunmez.

- **PORTATIF API (`grid.hpp/.cpp`, Windows basligi YOK):**
  `enum class GridViewPlane { XY, XZ, YZ }` + `gridViewPlaneForDirection(Vec3
  viewDirection)` (ARGMAX |bilesen|; **esitlikte Z kazanir** → izometrik 3B
  gorunuste plan gridi) + `gridAxesForView(grid, plane)` → `std::vector<GridAxisLine>`.
- **TEK KAYNAK:** `rebuildGridAxes(grid)` = `gridAxesForView(grid, XY)` + kat
  on/arka kenar cizgileri. **v10'dan itibaren CIZIM *ve* 3B SNAP geometrisi
  `gridAxesForView(grid, plane)`'dir (snap de gorunuse bagli — kullanici
  sarti: "XZ YZ ... her grid snap ozelligine kilitlenmeli"); `grid.axes`
  yalniz 2B snap fallback'i + `Document::bounds` (zoom-extents) icin kalir —
  orada gorunuse gore yapma, semantigini bozarsin.**
- **Gorunus duzlemi KAMERANIN KILITLI YONUNDEN gelir (v9, asagidaki bolum).**
  Eski (v8) yol canli `R_`'den turetiyordu; v9'da yon gorunus tabani
  kurulurken bir kez saklanir ve renderer `camera.gridViewDirection()`
  okur. Canli yonu yeniden turetme (`viewTransform(e_x).z` uclusu) YASAK:
  donuste grid cizimini degistirir.
- **Aile geometrisi:** XY → X koordinatlari Y boyunca + Y koordinatlari X
  boyunca (kat cizgisi YOK). XZ → X koordinatlari DIKEY (Z boyunca),
  Z kotlari YATAY (X boyunca); gorunus duzlemi `y = origin.y`. YZ → Y
  koordinatlari DIKEY, Z kotlari YATAY (Y boyunca); `x = origin.x`.
  `horizontal`: duzlem icinde yatay = true, Z-boyunca = false.
- **Elevasyonda dikey uzanti `extendBelow`/`extendAbove`'dan gelir** (plan
  gorunuste Y derinligi olan bu degerler kesitte Z uzantisidir). Kat kotu
  olmayan gridde bile dikey cizgiler boylece 2*uzanti boyunda cizilir.
- `renderer.cpp` grid blogu (drawGridAndAxes ICINDE → her pencerede):
  `gridPlane` kare basina BIR kez, sonra her grid icin
  `gridAxesForView(grid, gridPlane)`. **`for (const auto& axis : grid.axes)`
  yazma** — bu eski "3B grid" davranisidir (plan gorunuste kat cizgileri +
  kesitte yanlis aile).
- Testler: `tests/run_grid_view_plane.py` (ctest `model_maker_grid_view_plane`):
  (1) kaynak sekli (gridAxesForView/gridViewPlaneForDirection kullanimi,
  `grid.axes` cizimde YOK, render-only, tek kaynak); (2) **uretim cizim blogu
  VERBATIM** + GERCEK `src/grid.cpp` sahte kaydedici GDI sinirinda. Segment
  ailesi DUNYA yonunden siniflandirilir (afin projeksiyonda X→(65,0),
  Y→(-24,-24), Z→(0,65) ayrik) — yani iddia implementasyonun
  `horizontal` bayragina guvenmez. 7 davranis testi: plan/front/side/izometrik
  aile sayilari, etiketler (paralel ailenin etiketi CIKMAZ), determinizm,
  gorunmez grid, kat kotu olmayan grid. Mutasyon:
  `tests/mutate_grid_view_plane.py` → **7/7 yakalanir** (grid.axes'e donus /
  sabit XY / argmax kaldirildi / paralel aile cizildi / kat cizgisi yanlis
  eksende / yanlis matris elemani / gorunmez grid cizilir).
- **Harness tuzaklari (GDI sahte siniri):** `references/grid-snap-view-plane.md`
  sonunda — `using` bildirimleri, `CreateFontW`/`ExtCreatePen` imzalari, stok font,
  stub makrolar. Test "kosmadi" yanilsamasinin nedeni bunlar.

---

## GRID GORUNUS DUZLEMI KILIDI — "bir kez, donus degistirmez" (v9)

KULLANICI SARTI: "grid sadece XY veya YZ veya XZ ye yakinligiyla BIR KEZ tek
duzlemde cizilir, goruntunun 3d degismesi grid in cizilimini degistirmez."
v8 gorunus duzlemini HER KAREDE canli kameradan turetiyordu: 3B'de yorunge
donusu sirasinda 45 derece esiklerinde aile ANIDEN degisiyordu (sicrama).

- TEK KAYNAK: `Camera::gridViewDirection()` (camera.hpp). Yon gorunus tabani
  KURULURKEN bir kez saklanir — varsayilan yapici + `reset()` + `setView()` +
  `setViewBasis()`. **`rotate()`/`rotateAbout()`/pan/zoom kilidi
  DEGISTIRMEZ.** Standart gorunus / iki noktali gorunus DISKRET bir gorunus
  degisimidir → kilidi yeniler; yorunge donusu yenilemez.
- Kilit **Kamera icinde** saklanir (Application/ViewportState'te DEGIL): pivot
  gibi viewport takasinda kendiliginden tasinir, "viewport-basina alan" dort
  temas noktasina dokunmak GEREKMEZ. Deger `Vec3` (dunya bakis yonu) —
  `GridViewPlane` DEGIL, boylece cekirdek `Camera` grid.hpp'ye bagimli olmaz.
- **INDEKS TUZAGI (olculdu):** R_ row-major, dunya bakis yonu = **3. SATIR =
  `R_[6], R_[7], R_[8]`** (`viewTransform(e_x).z == R_[6]`). Yarim akilda
  kalip **`R_[2], R_[5], R_[8]` (SUTUN 2)** yazmak kayan noktada bazen
  "calisiyor" gorunur (kimlik taban ve Right gorunusunde ayni sonucu verir)
  ama **Front'ta sifir vektor**, ISO'da yanlis sonuc verir. Testler bunu
  yakalar (Front/ISO kilidi).
- **KAYAN NOKTA BERABERLIGI (olculen SESSIZ KUSUR, v8'de de vardi):**
  `gridViewPlaneForDirection` tam-esitlikle karsilastiriyordu
  (`az >= ax && az >= ay`). Gercek kameranin ISO yonu matematiksel olarak
  (1,1,1) ama bilesenler AYRI ifadelerden (R'nin 3. satiri) geldigi icin son
  bitte farkli: olcum `reset()` -> (-0.577350,-0.577350,-0.577350), tam
  esitlik testi **YZ** veriyordu → varsayilan 3B gorunuste plan yerine
  elevasyon ailesi ciziliyordu. Cozum: toleransli beraberlik
  (`kTieEpsilon = 1e-9`, `az >= ax - eps`). Sentetik harness bunu KACIRIR
  (sahte kamera (1,1,1)/sqrt(3) tam esit verir) — gercek `Camera` ile
  test edilmeli.
- Testler: `model_maker_grid_test` (78 kontrol; kilit bolumu: varsayilan/
  reset ISE plan, Front→XZ, Right→YZ, Top→XY, `rotate()` kilidi
  degistirmez ama kamera yonu gercekten degisir, `setViewBasis`→XZ, egik
  taban beraberligi, son-bit beraberligi) + `run_grid_view_plane.py`
  (kaynak-sekli: `camera.gridViewDirection()` okunur, `camera.viewTransform(`
  blokta YOK, kilit tam 3 yerde kurulur, `R_[6],R_[7],R_[8]`, `rotate`
  govdesinde lock yok) + 2 davranis testi (kilit canli rotasyonu ezer;
  3B donus ayni cizimi korur). Mutasyon `mutate_grid_view_plane.py`
  9/9 (M6 canli yonu geri koyar, M8 reset'te kilidi kaldirir, M9 yanlis
  matris satiri).

---

## GRID TUM SNAP TURLERINE YAPISIR (grid akslari cizilebilir geometridir)

KULLANICI SARTI (iki asama): "grid e tum snaplar yapismali" + "XZ YZ gibi 2
noktayla alinan goruntulerde grid sadece xy deki alt grid noktalarina snap
oluyor; her grid snap ozelligine kilitlenmeli".
**DETAY, OLCUM ve TUZAKLAR: `references/grid-snap-view-plane.md` (OKU).**

- TEK KAYNAK `addGridLineCandidates(candidates, document, cursor, tolerance,
  metric, referencePoint, screenSpace = nullptr, GridViewPlane plane = XY)`
  (src/drafting.cpp; `objectCandidates`'tan sonra). **Geometri kaynagi
  `gridAxesForView(grid, plane)` — CIZIMLE AYNI fonksiyon.** `grid.axes` (PLAN
  tabani; plan cizgileri z=origin.z'de) snap'te KULLANMA: elevasyonda snap
  taban kotuna kilitlenir. `snap3D` duzlemi KILITLI yonden turetir
  (`gridViewPlaneForDirection(camera.gridViewDirection())`); `snap()` (2B,
  kamerasi yok) XY kalir. Kesisim GERCEK cozulur (`lineCrossing`); eski
  `{x, y, z-XAkstan}` YALNIZ plan icindi. Gorunmez grid atlanir.
- Adaylar: Endpoint (a,b), Midpoint, Nearest (3B'de EKRAN uzayinda,
  `ScreenSpaceNearest`), Perpendicular (`t in [0,1]`); kesisim
  `SnapType::Grid` kalir (capraz isareti KORUNUR — `Intersection`'a cevirme).
- Cagri yerleri: **obje snap'inden SONRA**, eski toleranssiz kesisim
  fallback'inden ONCE. Fallback AYNEN korunur (kaldirirsan "grid artik
  yapismiyor" regresyonu). Maske kapisi sart: `choose(..., enabledTypes[,
  &camera])`; `enabledTypes` verilmezse kapatilan tur yine uretilir (test M2).
  `SnapTypeMask` varsayilani `fill(true)` + `gridSnapEnabled_{true}` → ACIK.
- Testler: `tests/grid_snap_types_test.cpp` (`model_maker_grid_snap_types_test`,
  **32 kontrol**; elevasyon testleri KATLI grid ister — zCoords'suz gridde
  duzlem-ici YATAY aile bos kalir ve snap `None` doner, yaniltici PASS) +
  `tests/mutate_grid_snap_types.py` (**13 mutasyon, 13/13**; M10 tam bu hatayi
  hedefler). Mutasyon deseni TEKILLIGI: `addCandidate(...,(a+b)*0.5,
  SnapType::Midpoint,...)` ve `const double clampedT ... Nearest` desenleri
  `objectCandidates` icinde de gecer — desenin sonuna ayirt edici satir ekle
  (`double rawT;` / `if (referencePoint) {`). Bir blogu mutasyonda KOMPLE sil;
  yalniz cagriyi silmek "undeclared gridCandidates" DERLEME hatasi verir ve
  test kosmaz (yanlislikla "kacirildi" gorunur).
