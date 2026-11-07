# Component Dialog + Parametre Editörü (universal, Tekla-tarzı)

## Katmanlar

1. Taşınabilir çekirdek `component_dialog.hpp/.cpp` (windows.h'siz, Linux'ta test edilir):
   - DiagramPrimitive / ComponentDiagramItem / DiagramHotspot / ComponentDialogGroup / ComponentDialogSchema
   - validateDialogSchema / hitTestDiagramHotspot / findHotspot
   - endPlateDialogSpecs / endPlateToParams / endPlateFromParams (27 alan adaptörü)
   - ComponentDialogRegistry + registerBuiltInDialogs (end plate + fin plate)
2. Qt kabuk `component_dialog_widget.hpp/.cpp` (Windows exe + Linux offscreen testi):
   - ComponentSchematicView: şemayı QPainter ile çizer, hotspot tıklamasını hitTestDiagramHotspot ile eşler.
   - ComponentDialogWidget: sol şema + sağ grup grup editörler; hotspot<->satır vurgusu çift yönlü.

## Kurallar

- Yeni birleşim ekleyen programcı YALNIZ dialog tanımını yazar: specs + şema (çizim/hotspot/grup) →
  ComponentDialogDefinition; registerBuiltInDialogs'a ekler. Editör/çizim/hit-test mantığı TEK KAYNAK
  (core + widget).
- Çizim ile hit-test AYNI şema verisini kullanır (hitTestDiagramHotspot) — tıklanan şey çizilen şeydir.
- Spinbox AÇIK aralığı (hasMin/hasMax false) -1e12..1e12 kullanır; -1e9 sentinel'i ("tanımsız" kenar
  mesafesi, end_plate.hpp'de < -1e8 = tanımsız) korur. -1e6'ya kelepçelemek sentinel'i SESSIZ bozar.
- resultParams() bilinmeyen (ekstra) parametreleri korur (engine felsefesi: sessiz kayıp yok).
- openComponentParamsDialog: ComponentDialogRegistry'de tanım varsa şematik editör, yoksa düz form
  (fallback). End plate kendi ayrı dialog'uyla (openEndPlateParamsDialog) gider; dialog registry'sindeki
  end plate şeması (beam_to_column_end_plate) şimdilik UI'ye bağlı değil.
- Yerleşik dialog kaydı application.cpp'de registerBuiltInDialogs(ComponentDialogRegistry::instance())
  ile (registerBuiltInComponents yanında).

## Tekla 146 duzeni (resimli bulon diyalogu) — kurallar

- `CompParamSpec.optional=true` → satir solunda "Default" onay kutusu. ISARETSIZSE parametre
  `resultParams()`'a YAZILMAZ (motor varsayilani uygulanir); kayitli deger varsa da SILINIR —
  aksi halde "Default" secmek eski degeri korur. Varsayilan `false` (mevcut diyaloglar etkilenmez).
- `ComponentDialogSchema.useTabs` + `ComponentDialogGroup.asTab` → gruplar SEKME olarak cizilir.
  Varsayilan `false`; end/fin plate eski bolum-kutusu duzeninde kalir.
- **AYNI PARAMETRE IKI SEKMEDE LISTELENMEZ**: widget parametre basina TEK editor tutar
  (`editors_` adla anahtarli). Ayni ad iki sekmede gecerse ikinci sekme BOS kalir ve atlanir
  (sekme sayisi duserva test kirilir). Bu yuzden 146'da ayri "Picture" sekmesi YOK; sematik
  sol panelde kalicidir, olcu alanlarini Plates/Bolts sekmelerine koy.
- Sekme icerigi **QScrollArea** icine alinmali: 146 "Bolts" 22 satir → duz QWidget tasar.
  Testte `findChild<QTabWidget*>()` + `tabText(i)` ile sekme adlari/sirasi dogrulanir.
- Hotspot tiklamasi sekme degistirmeli: `paramTabIndex_` ile once `setCurrentIndex`, sonra satir
  vurgusu. Sekmeli modda `scroll_` YOKTUR — `ensureWidgetVisible` cagrisini `if (scroll_)` ile koru.
- 146 semasi engine'de 32 parametre (8 core + 16 Tekla + 8 bulon ozelligi: boltStandard,
  tolerance, threadInMat, holeType, rotateSlots, slotsIn, cutLength, comment). Parametre sayisini
  dogrulayan testleri (shear_plate_146_test, component_dialog_test) birlikte guncelle.
- Hotspot koordinatlari sema degisince kayar; testte koordinat yerine hotspot id'sini tercih et
  veya koordinati semayla birlikte guncelle.

- **KAYNAK TABLOSU (Tekla "Welds")**: `schema.weldRows` (WeldTableRow) + `DiagramHotspot.weldRow`.
  Kural: bir hotspot YA parametreye YA kaynak satirina baglanir — ikisi birden olmaz
  (`validateDialogSchema` reddeder). Satir numaralari 1..N SIRALI olmali; cagri etiketi
  (`h.label`) satir numarasiyla ayni olmali, aksi halde resimdeki numara tabloyla kayar.
  Widget tarafi: `Welds` sekmesi 12 sutunlu QTableWidget; No sutununda onay kutusu (enabled);
  satir secimi `ComponentSchematicView::setHighlightedWeldRow` ile cagriyi vurgular, cagri
  tiklamasi `weldCalloutClicked` -> satir secimi + Welds sekmesine gecis. Duzenlenmis satirlar
  `resultWeldRows()` ile geri alinir (testte onay kutusu/olcu round-trip'i dogrulanir).
- Tekla 146 diyalogu artik 11 sekme (68dbd8c): Picture / Plates / Stiffeners / Haunch / Notch /
  Bolts / Beam cut / Angle box / BoxPBolts / BoxSBolts + Welds. Kaynak: kullanicinin yukledigi
  conn_146_help PDF'i (~/uploads). General / Design type / Analysis sekmeleri PDF'te yalniz link,
  alan listesi YOK — uydurma, kullanicidan ekran goruntusu/alan listesi iste. BoxP/BoxS ayni
  alanlari tasir: ayni ad iki sekmede olamayacagi icin onek (`boxP*`/`boxS*`) zorunlu. Sekme
  sayisi degisince `component_dialog_test` (groups.size) + `component_dialog_widget_test`
  (count, tabText, pixLabels esigi) birlikte guncellenir.
- **Gercek Tekla ekran goruntuleri (b594d13)**: 146 diyalogu 13 sekme, Tekla sirasiyla: Picture,
  Plates, Stiffeners, General, Haunch, Notch, Bolts, Design type, Beam cut, Angle box, BoxPBolts,
  BoxSBolts, Analysis. "Welds" SEKME DEGIL: ust cubukta "Welds" onay kutusu + "Welds..." dugmesi
  (kaynak tablosu ayri QDialog; onay kutusu kapaliysa resultWeldRows hepsini kapali doner).
  Ust cubuk: Save/Load/preset/Save as/Help... + "ignore other types"; alt cubuk (widget->
  createButtonBar): OK Apply Modify Get [x/o] Cancel .... Create. Karsiligi olmayanlar GRI.
  Image-dropdown simgeleri, sekme icindeki serbest yerlesim ve Picture sekmesine gomulu sema
  YAPILMADI (sema sol panelde kalici).
- PARCA SATIRI: `ComponentDialogGroup.partRows` (DialogPartRow: t b h | profile | Pos_No
  [+ asm ikinci satir] | Material | Name | Finish) sekmenin USTUNDE QGridLayout ile cizilir; her
  hucrenin Default onay kutusu = ayni mekanizma (optionalChecks_), gri hucrenin kutusu gri.
  Parca satiri parametreleri paramNames'e YAZILMAZ (tek editor kurali); validateDialogSchema
  partRows adlarini da dogrular. `keepIfEmpty`+`emptyNote`: bos ama Tekla sirasini koruyan sekme.
- Ekran goruntusunden okunan, PDF'te OLMAYAN alanlar: General (Up direction auto + 2 kutu,
  Locked No, Class 99, Connection code, AutoDefaults/AutoConnection rule group None), Design type
  (External design, Check connection, Design code, Load definition, UDL %, Loads from secondary
  V/T/M). Dropdown'larda yalniz GORUNEN deger yazildi (diger secenekler uydurulmadi).
  Manuel notch ust/alt flans icin AYRI alan seti (Top/Bottom son ekli).
- BoxPBolts/BoxSBolts/Analysis ekran goruntuleri GELDI (sonraki commit): `ComponentDialogGroup.layout=
  L"boxbolts"` + `prefix` (boxP/boxS) ozel yerlesim: sol cerceveli kutu (Bolt size/standard/
  Tolerance/Thread in mat/Top/Bottom Site-Workshop) + Comment + "Top"/"Bottom" kutulari (SlotV/H,
  Hole type, Rotate Slots, Slots in, Length increase; her biri Top/Bottom onekli ayri param) +
  "Bolt group dimensions" (Pos1/Edge/Count/Spacing/Pos5). Analysis: 13 satir; paramNames girdisi
  `"a|b"` AYNI satirda iki editor (Ux [Free v] [0.00]; onay kutusu ikisini yonetir). Acilir
  listelerde goruntude yalniz bir deger gorundugu icin yalniz o yazildi; Restrain combination
  resimli liste, secili ogesi kalem simgesi (U+270E). Resimler (destek kosulu eksen sekli, BoxP/S
  semalari) goruntu dosyasi olmadigi icin YOK; "..." katalog dugmeleri YOK.
  `a|b` kullaniminda mapName/validate/ungrouped icin entry'yi bolmeyi unutma.
- Tekla PDF'inden sekil cikarma: `pdfimages -list` sayfa->resim eslemesi, `pdfimages -png`;
  sayfa metni `pdftotext -layout`. vision_analyze bu host'ta calismiyor; resim icerigini
  gorduyormus gibi raporlama.

## Ayar setleri — Tekla Save / Save as / Load (genel prosedür)

Adlandırılmış PARAMETRE setleri; kullanıcı kataloguyla (`user_catalog`, yeni bileşen tipi türetir) karıştırılmaz.
- Çekirdek: `component_presets.hpp/.cpp` → `ComponentPresetStore` (typeId + ad → `vector<CompParam>`),
  JSON `component_presets.json` (exe'nin yanında), atomik yazım (.tmp + rename), Qt'siz, Linux testli.
  Yalnız parametre değerleri saklanır; seçili kolon/kiriş/kesit SAKLANMAZ.
- `standard` her zaman listenin başında. Kayıtlı değilse Load = şema varsayılanları (`setParams({})`;
  "Default" kutulu alanlar sonuca yazılmaz — beklenen). Kayıtlı `standard` menüden yeni yerleştirilen
  bileşenin başlangıcıdır (`standardComponentParams` in qt_main_window.cpp).
- Diyalog: `ComponentDialogWidget::setPresetStore(store, persist)`; Save=seçili seti üzerine yaz,
  Save as=ad kutusundaki adla (kırpılır) yeni set, Load=`setParams(set)` (canlı önizlemeyi de günceller).
  Listeden seçmek de HEMEN yükler (`QComboBox::activated`, yalnız kullanıcı etkileşiminde yayılır;
  `currentTextChanged` yalnız ad kutusunu doldurur). İlk sürüm yalnız Load düğmesiyle yüklüyordu ve
  kullanıcı "seçince değerler gelmiyor" dedi: UI'de seçim = yükle beklenir. Testte `activated`ı
  `QMetaObject::invokeMethod(combo,"activated",Q_ARG(int,i))` ile tetikle.
  Depo bağlanmazsa düğmeler gri (eski davranış). Her değişiklik `persist` ile diske yazılır; disk hatası
  durum satırında açıkça gösterilir. Geçersiz ad (boş, >64, kontrol karakteri) kaydetmez.
- Yeni bileşen/diyalog eklerken yapılacak ek bir şey YOK: anahtar typeId olduğu için her ComponentDialogWidget
  tabanlı bileşen otomatik kazanır. Tek koşul: iki açma yerinde (modeless+modal) `setPresetStore` çağrılır.
- Bozuk `component_presets.json` SESSİZCE ezilmez: önce `.bozuk` yedek.
- SINIR: kaynak tablosu satırları (146 Welds...) set'e yalnız `createWelds`/`weldSize` olarak girer, satır
  bazlı özellikler geri gelmez. End plate kendi ayrı diyaloğuyla gider, bu mekanizmayı kullanmıyor.
- Test tuzağı: "Default" kutulu/editorsuz parametre `resultParams()`'a yazılmaz; gidiş-dönüş testinde
  önce o parametrenin gerçekten gidip geldiğini prob ile seç (plateThickness 146'da yazmaz).
- Tuzağı: qt_main_window.cpp Linux'ta derlenemez; eklenen yardımcı bloğu Windows derlemesi doğrular.

## Doğrulama

- Taşınabilir çekirdek: tests/component_dialog_test.cpp (Linux ctest).
- Qt widget: tests/component_dialog_widget_test.cpp — Qt6 varken Linux'ta OFFSCREEN koşar
  (QT_QPA_PLATFORM=offscreen); round-trip + sentinel + bilinmeyen-param kontrolü. Windows'ta görsel
  Asus'ta model-maker exe'siyle (fin plate çift tık → şematik diyalog).
