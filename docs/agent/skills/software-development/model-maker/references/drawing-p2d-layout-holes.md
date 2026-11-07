# P2D (P1) — akıllı pafta sığdırma + bağımsız (elle açılmış) delikler

Çizim hattı P1→P2A→P2C→P2B ilerledi; **P2C (akıllı pafta yöneticisi) P2B'nin ALTINDA**, yani
`feature/p2b-paper-space-editor-20261027` (21935d6) zaten tek-pafta kalite kapısı + çoklu pafta
iskeletini taşır. Yeni pafta işi "sıfırdan yönetici" değil, `drawing_set.cpp` içindeki bölmeyi
iyileştirmektir. Önce `git log --oneline` ile hangi katmanın nerede olduğunu doğrula.

## Pafta sığdırma (src/drawing_set.cpp)

- Eski `splitSheet`: her detay görünüşü KENDİ paftasına (kabul edilemez). Yeni `packSheets`:
  1) ana görünüşler tek paftada, sığmazsa sondan devam paftasına;
  2) ana paftaya sığdığı kadar detay eklenir;
  3) kalanlar "önce-sığdır" ile doldurulur (en az sayfa), detaylar `(detailPlate, viewId)` sırasıyla gruplanır;
  4) sığmayan görünüş kendi paftasında `NO_FIT` — sessiz düşme yok.
- `tryPack` = `subsetOf` + `applyPreferences` + `layoutOk`; her deneme tam yerleşim çalıştırır.
  Görünüş sayısı ~10 ise O(n²) yerleşim kabul edilebilir (tek çağrı ~0.3-1 s).
- `finish()` her paftada yerleşimi TEKRAR çalıştırır; alt küme aynı olduğu için sonuç deterministiktir.
- Yeni: `DrawingSetResult::plan` (numara/kâğıt/ölçek/görünüş/sebep) + `statusText()` (Qt'siz).
  GUI ve testler aynı metni kullanır; tek-pafta yolunda sebep `paperReason(sheet)`'ten gelmeli
  (sabit string yazma — "buyutuldu A3->A2" bilgisi kaybolur).

## SheetSize enum tuzağı

`SheetSize` A4<A3<A2<A1<A0 (A0 en büyük). `autoMax=A2` "en fazla A2" demektir; `autoFirst=A3` ile
min=A3,max=A2 olur ve `layoutSheet` {A3,A2} dener. Ters aralık (first>last) sessizce first'e çöker.
Test yazarken beklenen kâğıdı bu sıraya göre hesapla: 2400 mm @1:4 → 600 mm, A2'ye sığmaz A1'e sığar;
2400 @1:3 → 800 mm, A1'i de aşar A0'a çıkar.

## GUI'de düzenleme koruma (drawing_sheet_widget.cpp)

- `baseline_` = **yeniden üretilen TEMİZ paftalar** (merge'den ÖNCE). Merge sonrası baseline alınırsa
  `collectEditedSheets()` bir sonraki değişimde düzenlemeyi "yok" sanır ve onay/koruma çalışmaz.
- Eşleştirme PAFTA İNDEKSİ ile yapılmaz: sığdırma bir görünüşü başka paftaya taşıyabilir. Her yeni
  paftaya TÜM düzenlenmiş paftaların `mergePaperEdits(..., withShapes=false)`'u uygulanır; anahtarlar
  kaynak öğe (görünüş/ölçü/mark/metin) tabanlı olduğu için doğru yere düşer.
- `PaperShape` sayfa düzeyindedir: indeks varsa o paftaya, sayfa azaldıysa ana paftaya `appendPaperShapes`
  ile (ElementId çakışması yeniden atanır, aynı şekil çiftlenmez).
- `preservePaperEdits` şekilleri HER çağrıda EZER (`regenerated.shapes = edited.shapes`) — çok paftalı
  birleştirmede şekiller ayrı yoldan eklenmezse kaybolur/çiftlenir.
- Merge sonrası `refreshSheetQuality` (bağımsız `measureOverlaps`) çağrılmalı; yoksa kullanıcı
  düzenlemesi çakışma yaratsa da pafta "READY" görünür. Kalite bozulursa `USER_EDIT_OVERLAP` + TASLAK.
- Yıkıcı değişiklik = kaynak eşleşmesi olmayan yeniden üretim (konu değişimi). `setRebuildConfirmation`
  ile enjekte edilebilir onay kullan (testte modal açılmaz); ret → konu geri alınır, çizim korunur.

## Bağımsız (elle açılmış) delikler (fabrication.cpp)

- Kural: `owner == kInvalidEntityId` "bilinmiyor" DEĞİL, "bağımsız delik" demektir. Envantere girer;
  aynı eksen/derinlik/levha-zarfı doğrulamasından geçer.
- Kimlik: `kAnonymousGroupBit (1<<63) | FNV(plate, dia, slotAlong, slotAcross)`; gerçek bulon grubu
  kimlikleriyle çakışırsa `++id` ile deterministik sondalama. İmza→kimlik eşlemesi tek extract içinde
  `map` ile tutulmalı, yoksa aynı grup iki kimlik alır.
- Ölçülendirme (drawing_builder): düzgün dizilimde mevcut adım+net kenar zinciri; düzensiz dizilimde
  `HoleOrdinate` ile her deliğin levha min-u/min-v kenarından GERÇEK merkez koordinatı (2 ölçü/delik).
  Sahte periyodik adım üretme. `HOLE_PATTERN_IRREGULAR` artık TASLAK nedeni değil; çerçeve yok/eğik/
  kısmi derinlik/yuva konturu yok → UNRESOLVED + TASLAK.

## P2 — bant secimi + snap'li grup tasima (cdd3a6d)

- Bant modu YON ile belirlenir: soldan saga = `Window` (tam kapsama), sagdan sola = `Crossing`
  (kesisim). GUI'de `minDragMm = 2 mm` esigi sart: yoksa tek tik "sifir bant" olarak bos secim yapar.
- **Gorunusler (View) bantla SECILMEZ** (buyuk gorunus Crossing'de her seferinde secilip yanlislikla
  tasinir); gorunus tiklayarak secilir. Bant secimi: sekil → metin → mark → olcu (deterministik).
- Secim cercevesi ile bant testi AYNI kaynagi kullanmali: `paperElementBounds`. Olcu icin
  `resolveDimension(...).textBox` bir **`Obb`**, dogrudan `Bounds2` DEGIL → `Obb::aabb()` sart
  (yoksa derleme hatasi: "could not convert Obb to Bounds2").
- Grup tasima TEK geri-al adimi: `PaperEditHistory::begin()` → `movePaperElements(sheet, sel, delta)`
  → `commit()`. `movePaperElements` tasinan eleman sayisini doner (0 → hicbir sey degismedi, abort et).
- Esc icin `PaperEditHistory::cancel()` kullan (`abort()` DEGIL): `abort` snapshot'i atar, geometri
  canli kalmis olur; `cancel` `sheet_ = pending_` ile geri doner ve yigini kirletmez.
- Snap toleransi **mm** cinsinden verilir (GUI: `6 px × mmPerPixel()`); tolerans 0 → snap yok.
  Aday listesi: sekil ucu/orta/merkez/kadran, gorunus origin+koseler, metin, mark, cerceve, cizim alani.
- Arac kutusu indeksleri GERIYE DONUK uyumlu tutuldu: `1=Cizgi, 2=Daire, 3=Merkez`; `Tasi` = **4**.
  Mevcut GUI testleri indeks 1'i cizgi sanar; araya arac sikistirmak onlari bozar.
- Cizim hem tik-tik hem surukle-birak: birakista mesafe >= 1 mm ise tamamla, degilse bekle
  (ayni kod hem eski surukleme testlerini hem yeni tik-tik akisini gecer).
- Onizleme (bant/secim/snap isaretcisi) `paintEvent` icinde `paintSheet` SONRASI ayri katman olmali.
  `paintSheet`'e KOYMA: PDF/PNG ayni fonksiyonu kullandigi icin onizleme ciktiya sizardi.
- GUI testte PDF sayfa sayisi: `pdfinfo` ciktisini `Pages:\s*(\d+)` regex'i ile ayikla; sabit bosluk
  sayisi varsayma (`"Pages:         1"` gibi contains kontrolu kirilgan) ve beklenen degeri
  `doc->sheets().size()` ile karsilastir.
- `set_tests_properties(... ENVIRONMENT QT_QPA_PLATFORM=offscreen)` satiri bir donem YANLIS hedefe
  (`component_dialog_widget_test`) yaziliydi; yeni GUI testi eklerken bu satiri kontrol et.

## Poz etiketi biçimi (2026-10-29, df34bb4)

- Mark etiketi `mark/profil` biçiminde: part mark kendi `profileName`'ini, assembly mark ana parcanin
  profili kullanir (ör. `P11/PL10*100`, `A1/IPE240`). `markLabelText(reg, mark, mainPartProfile)`
  (drawing_builder.cpp); profil bos ise yalniz mark yazilir (eski davranis). `sheet.name` (antet) DEGISMEDI.
  Test: `tests/drawing_test.cpp` A ("P1/IPE300") ve C ({"A1/IPE240","P1/IPE240","P2/PL12"}).

## TEK PAFTA / KAGIT TASMASI + DUZENLI MARK (2026-10-29, d0df74d) — kalici

Kullanici istegi: "tek paftaya sigacak cizim yine bolunuyor", "otomatik yerlegirmeyi iptal edelim,
kagittan tassa da cizim tek yerde duzenli dursun, sonra kagit secip Uygula deyince yeni kagida yerlcssin",
"part mark'lar daginik geliyor".

- `DrawingPreferences::autoMultiSheetFallback` **varsayilan false** (bolme yalniz acikca istenirse; eski
  bolme testleri bunu elle set eder). Yeni `allowOverflow` (varsayilan true) ve `LayoutConfig::
  allowOverflow` / `LayoutReport::overflow` alanlari.
- `buildDrawingSet` artik kalite eksik olsa da TEK pafta dondurur (`ok=true`, `layoutValid=false`);
  eski "tek pafta saglanamadi" hatasi YOK. Bolme yolu aynen korunur (packSheets).
- `layoutSheet` kabul sirasi: (1) kagida tam sigan + cakismasiz, (2) kenardan <=%15 tasan ama
  cakismasiz, (3) kagida sigan ama bazi olcu/etiket yerlestirilememis (okunabilir olcek korunur),
  (4) tasan + cakismasiz. Tasan denemeler arasinda %5 tolerans: gereksiz kucultme yok.
  `placeViewsWithMargins(..., allowOverflow, &overflowed, &excess)` tasma orani dondurur.
- `Scene::allowOutside` YALNIZ blok kagidi astiysa true (annotation cerceve disina cikabilir).
  Blok sigiyorsa eski kati kural (her sey cerceve icinde) korunur — yoksa temiz yerlesimler bozulur.
- `drawing_set.cpp::blockingOverlaps()`: tasma kabul edilen paftada sadece `outsideBorder` affedilir;
  metin/metin, metin/cizgi, olcu cakismalari asla. `refreshSheetQuality` + `validateDrawingSet` ayni kural.
- MARK DUZENI: `placeMark(..., slot, useSlots)` — ayni gorunusun mark'lari poz numarasina gore
  SABIT SLOT (sag kolon -> sol kolon -> ust/alt satir). Yerlestirilemeyen varsa sirayla: depolama
  sirasi + slot, sonra slot'suz eski serbest arama (etiket KAYBI yok). Slot duzeni once denendigi icin
  etiketler sayfaya dagilmaz (olculdu: ort. lider 57 -> 29 mm).
- DETAY BLOGU `layoutDetailBlock(..., maxWidth)` ile SATIRLARA SARILIR; sarma sonrasi blok hala
  sigmiyorsa `tryLayout` detaylari pafta olcegine indirip IKINCI gecis yapar. (4 detay 1:2'de tek
  satirda A3'e sigmadigi icin her deneme NO_FIT oluyor ve pafta bolunuyordu — asil kok neden.)
- Olcu zinciri satir denemesi 3 -> 8.
- Teshis: `MM_LAYOUT_DEBUG=1` her deneme icin `[att] <kagit> 1:<den> ok/ovf/excess/issues` ve
  iki gecis icin `[try]` satiri basar; `[layout] NOFIT avail/main/det/...` blok olculerini verir.
- Test sozlesmesi degisti: `drawing_sheet_fit_test` A artik "issues bos" degil "tek pafta; eksik varsa
  acikca raporlanir" der (HEAD'de bu fixture zaten bolunuyordu).
- Performans: montaj basina ~1 dk (75 kagit/olcek denemesi x izole marj). Erken kesme optimizasyonu
  HENUZ yok; buyuk modellerde uretim yavasi.

## Degisim noktasi kesitleri (2026-10-29, e490d68)

- `DrawingOptions::sectionViews` varsayilan ACIK; eski main/detail gorunus sayisi veya kagit
  boyutu bekleyen testler fixture'da bu secenegi acikca KAPATMALI. Yeni davraniş
  `tests/sections_test.cpp` ile ayrica dogrulanir; testi kapatmak uretim varsayilanini degistirmez.
- `changePoints` uye eksenindeki profil vertex'lerini, temas eden levha zarfi sinirlarini,
  bulon merkezlerini, component sinirlarini ve kaynak origin'ini 1 mm toleransla birlestirir.
  Uzak levhayi yalniz eksene izdusererek kesit nedeni SAYMA: enine zarfta temas da ara.
- `generateSectionView` tel kafes kenarlarindan dilim ici segmentleri; ara konumda yalniz
  boyuna kenarlar noktaya dustugu icin yuz/duzlem kesisiminden GERCEK kesit konturunu uretir.
  Yerel geometri + 0.1 mm imza, yon/konumdan bagimsiz dedupe, ilk nokta provenansi.
- `drawing_layout.cpp::tryLayout` her denemede gorunus olcegini yeniden atar: Section
  1:1'i ATLAMALI. Detay blogu yeni kesit satirini kagit genisligine sarmali ve dar kesitin
  uzun basligi icin TAM metin genisligi ayirmali; aksi halde basliklar komsu kesite tasar.
- Gercek `.mmw` probe'u uretim ve layout kalitesini AYRI raporlamali: `ok=true`, tek pafta
  ve 1:1 olcek, `layoutValid=false` ile birlikte gorulebilir. Montajda kesitsiz baseline da
  DIMENSION_UNPLACEABLE verebilir; yeni kesit bir baska zincir sorunu ekliyorsa gizleme.

## Olcu metni ve zincir kuralları (kullanici geri bildirimi 2026-10-28, db413de)

- **Olcu metni YALNIZ tam mm**: `dimensionText()` (drawing_layout.cpp) ve `fmtNum()`
  (drawing_builder.cpp) her zaman `%.0f` + `std::round`. Eskiden 0.05 toleransı dışında `%.1f`
  yazıyordu -> çizimde 3591.3 / 429.2 / 14.6 gibi alt-mm değerler çıkıyordu. İki fonksiyon AYRI
  dosyada kopya; birini düzeltip diğerini unutma (fab GUI testi `dimensionText` ile karşılaştırır).
- **Aynı ölçü çizgisi = TEK OBJE**: `expandPaperSelection(sheet, sel)` — bir ölçü hit'i aynı
  `(viewId, chain)` zincirindeki tüm görünür segmentlere genişletilir. `selectPaperElements`
  dönüşünde, Select tık seçiminde ve Taşı aracının ilk seçiminde çağrılır. `chain == 0` olan ölçü
  genişletilmez. Genişletme ZİNCİR BAŞINA yapılır; başka zincire sızmamalı (test J bunu kontrol eder).
- **Eksen ölçüsü "havada" kalmasın**: `memberLength` YALNIZ eksen uçları görünen gövde uçlarıyla
  çakışıyorsa (<=0.5 mm) yazılır. Eksen modelde daha uzun olabilir (uçlar kolon yüzünde, gövde 95 mm
  içeride) → o zaman ölçü çizgisi parçanın dışına taşar ve kullanıcı "hangi uzunluğa ait belli değil"
  der. Bu durumda eksen ölçüsü ATLANIR, `OverallWidth/Height` (görünen gövde) ölçülür.
  Teşhis: kullanıcının `.drawing.json`'unda `role` + `basis` alanına bak (grep 5000 koordinatları da
  yakalar — `python3 -c` ile `dimensions` içinde `round(value)==5000` süz).
  Test: `tests/drawing_test.cpp` E bölümü.
- **Uzun ölçü tekrarı YOK**: bir görünüşte aynı yönde `MemberLength` varsa `OverallWidth/Height`
  (view bounding box) EKLENMEZ. Aksi hâlde 5000 (bbox) + 4810 (üye boyu) üst üste biniyor ve
  kullanıcı "dıştan dışa 5000 alakasız" diyor. Dış-tan-dışa toplam imalat zincirinden okunur.
  Test: `tests/drawing_test.cpp` A bölümü (`memberLenDims >= 2 && !longDup`).

## Gizli çizgi (oklüzyon) — gerçek HLR (2026-10-28, d7ce6dd)

- Eski `hiddenEdges()` **arka-yüz testi**ydi: parçanın kendi arka kenarları gizli sayılıyor, önünde
  duran başka parça (assembly levhası) görülmüyor, kısmi örtülme ifade edilemiyordu. Yerine:
  görünüşe giren TÜM parçaların yüzleri 2B üçgen + derinlik olarak toplanır (`collectViewTriangles`),
  kenar ~2 mm örneklenir, daha yakın üçgen varsa nokta gizli, geçişler 8 adım bisection ile
  hassaslaştırılır ve kenar görünür/gizli **parçalara bölünür** (`classifyHiddenRuns`).
- Derinlik işareti: `dot(p, fr.normal)` ve **büyük = izleyiciye yakın**. `Front` normali `-Y`.
  Aynı yüzeydeki (ortak) kenar kendini örtmesin diye `kDepthEps = 0.05 mm`.
- **TUZAK: dedupe "visible wins" tek başına yetmez.** Arka kenar ön kenarla aynı izdüşüm çizgisine
  düşünce ikisi de çizilir → çizgi kesik+girift görünür ve toplam uzunluk şişer (ölçüldü:
  11160 → 19160). Çözüm: gizli aralıklar, aynı çizgideki GÖRÜNÜR aralıklardan **çıkarılır**
  (kollinear test: yön kosinüsü + normal offset toleransı 0.05 mm).
- 0.05 mm altındaki kıl (sliver) parçalar çizime girmez — bisection artıkları.
- Arka yüzdeki delik daireleri kesik çizilir: merkez + 4 kadran örtülme örneği (`Circle2.style`).
- **Test tuzağı:** eski `drawing_test` E kontrolü "segment sayısı gizli çizgiyle değişmez" diyordu;
  kenar bölündüğü için artık geçersiz. Yeni invaryantlar: kıl parça yok + çakışan arka kenar ikinci
  kez çizilmez (toplam uzunluk artmaz). Gerçek assembly senaryosu J bölümünde (ön levha → kısmi
  gizli, toplam uzunluk eşit).
- Maliyet: O(kenar × örnek × üçgen). 2 mm örnekleme + üçgen 2B bbox ön elemesi büyük assembly'de
  kabul edilebilir; daha büyük modellerde 2B grid gerekir (henüz yok).

## Test tuzakları

- Detay görünüşü YALNIZ assembly veya çok parçalı subject'te üretilir (`opt.detailViews && (Assembly || parts>1)`).
  Tek başına döndürülmüş levha için hiçbir görünüş levha yüzüne bakmaz (`facing < 1-1e-6`) → ölçü çıkmaz.
  Döndürülmüş levha testini ASSEMBLY subject'i + levha DETAY görünüşü ile kur.
- Qt offscreen'de fare ile ölçü taşıma testi: sürüklemeden sonra `dlg.currentSheet()` (canlı
  `PaperEditHistory::sheet()`) oku; `currentDocument()` yeniden üretime kadar BAYATTIR. Ayrıca önce
  sadece `MouseButtonPress` gönderip `view->selection().kind == Dimension` doğrula; metin/mark önde olabilir.
- PDF determinizmi: `QPdfWriter` `/CreationDate` yazar → bayt-bayt eşitlik BEKLENMEZ. Determinizmi
  `pdfinfo` sayfa sayısı + `pdftotext` metni + JSON `serialize()` eşitliği ile doğrula.
- Bu host'ta tam build ~8-10 dk (2 vCPU). Önce hedefli derle: `cmake --build build-hermes --target <test>`.
  `-DCMAKE_DISABLE_FIND_PACKAGE_OpenCASCADE=ON` bu commit'te kullanılmıyor (uyarı verir, zararsız).
- Windows doğrulaması burada yalnız çekirdek kaynaklar için mümkün:
  `x86_64-w64-mingw32-g++ -std=c++20 -fsyntax-only -D_USE_MATH_DEFINES -I include -I third_party <src>`
  (Qt'li `drawing_sheet_widget.cpp` MSYS2 Qt olmadan derlenmez — raporda açıkça yaz).
