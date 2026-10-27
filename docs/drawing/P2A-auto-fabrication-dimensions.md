# P2A — Otomatik imalat olculeri + akilli sifir-duzenleme sayfa yerlesimi

Durum: cekirdek (OCCT'siz, Qt'siz, Linux ctest'te kosar) + Qt onizleme/PDF katmani. Kapsam **bilincli olarak dardir**:
desteklenmeyen her sey `Unresolved` olarak raporlanir ve cizim **TASLAK / EKSIK** damgasi tasir. Imalata hazir ILAN EDILMEZ.

## Katmanlar (geometri != annotation != yerlesim)

| Dosya | Rol |
|---|---|
| `include/model_maker/fabrication.hpp`, `src/fabrication.cpp` | `fab::extractFabrication`: Document -> tipli `MemberDatum`, `PlateFeature`, `HoleFeature`, `HoleGroupFeature`, `Unresolved`. Cizimden BAGIMSIZ. |
| `src/marking.cpp` / `PartSignature::drillHash` | Delik/kesme parmak izi imzaya katilir: ayni sekilli, FARKLI delikli iki levha FARKLI mark alir. Delikli olmayan parcanin imzasi degismez (`drillHash == 0`). |
| `include/model_maker/drawing.hpp` | Sema: yeni `DimensionRole`lar, `Dimension2D` provenance + duzenlenebilir yerlesim parametreleri, `TextAnnotation`, `FabricationStatus`/kontrol listesi, `LayoutConfig`/`LayoutReport`, `ViewFrame`, `ViewKind::Detail`. JSON v2 (v1 okunur). |
| `src/drawing_builder.cpp` | Olcu/callout URETIMI (inventory'den), uye-hizali gorunum cercevesi, DETAY gorunumleri, kontrol listesi. |
| `include/model_maker/drawing_layout.hpp`, `src/drawing_layout.cpp` | Yerlesim motoru (kagit mm, deterministik) + BAGIMSIZ `measureOverlaps`. |
| `src/drawing_sheet_widget.cpp` | Onizleme + PDF: `resolveDimension/resolveText/markBox` ile AYNI nihai plani cizer (tek yol). |

## Tek-kaynak kurallari

1. **Imalat deligi asla 2B daireden/bulon basindan/bbox'tan cikarilmaz.** Kaynak: gercek `CutModifier(Hole)` + sahibi `BoltGroup`;
   levha cercevesi: kalici `ComponentInstanceRecord::children[].plate` (`PlateSpec`). Cerceve kati modelle dogrulanir
   (`PLATE_FRAME_STALE`); cercevesiz levhadaki delik `HOLE_FRAME_UNAVAILABLE`, sahipsiz delik `HOLE_OWNER_UNKNOWN`.
2. **Koordinatlar:** levha-yerel (u,v) = `(P - PlateSpec.center) . u/v`; uye-yerel (x',y',z') baslangic = `axisFrom`,
   x' = eksen, z' = dunya +Z'nin x'e dik bileseni, y' = z' x x' (konvansiyon; `profileRotation != 0` -> `MEMBER_ROLL_UNRESOLVED`).
3. **Gercek uzunluk kurali:** delik/levha olculeri yalniz levha yuzunu gosteren (|n . gorunum.normal| = 1) gorunumde;
   uye ofsetleri yalniz o ekseni gorunum duzleminde tutan gorunumde. Her imalat olcusunde `Dimension2D::measured() == value`.
4. **Net kenar:** `HoleEdge` = levha zarf kenari -> en dis deligin DIS kenari; iki bagimsiz grup arasi net aralik ayri olcu.
5. **Yinelenen zincir yok:** ayni gorunum/yon/uc noktali olcu tek kez; imalat rolu genel olcuyu yener; cozumlenmis levha
   delikleri icin P1 yedek `HoleSpacing` zinciri uretilmez.
6. **Provenance:** her imalat olcusunde `featureKey` (`plate:<id>`, `group:<plate>.<boltgroup>.<dia*10>`, `hole:...`),
   `basis`, `documentRevision`, `source` (EntityId). JSON'a yazilir.

## Yerlesim motoru

* Her sey KAGIT mm; yazi buyuk-harf yuksekligi: olcu >= 3.0, not/callout >= 3.5 (yapilandirilabilir; altina inilirse `TEXT_TOO_SMALL`, sessiz kabul yok).
* Sert: metin/metin yok, metin x (olcu/uzatma/kilavuz cizgisi) yok, metin x gorunum siniri/title/cerceve yok.
* Aday arama: olcu zinciri iskeleti (uzatma+olcu cizgileri) once sahneye, sonra metin adaylari
  (orta, uc-disi, 5 kademe, iki yan) ilk cakismasiz aday; satir yigma (genis aralikli zincir ice, dar aralikli disa).
* Mark/callout: halka + gorunum-kenari adaylari, en dusuk maliyet (kilavuz uzunlugu + kesisim).
* Olcek/kagit: standart olcek (buyukten), sigmazsa/cakisirsa kucuk; sonra A3 -> A2 -> A1 (`minSheet`/`maxSheet`).
  Delikli levhalar assembly'de kendi olcekli **DETAY** gorunumune konur (ana olcekten ayni/buyuk).
* Gorunumler annotation payiyla birlikte yeniden yerlestirilir (iki dizilim: detaylar sagda / ustte).
* Sigmazsa: `layout.ok == false`, `issues` (`NO_FIT`, `DIMENSION_UNPLACEABLE`, ...), cizim TASLAK; **yazi kucultulmez, cakisma gizlenmez**.
* Model DEGISMEZ: yerlesim yalniz `offset/side/textT/textSide/textLevel`, `TextAnnotation::pos`, `MarkAnnotation::labelPos`, gorunum `scale/origin`, sayfa boyutunu yazar.

## Kontrol listesi (imalat verisi tamligi)

`PART_MARK`, `ASSEMBLY_MARK`, `MEMBER_DATUM`, `PLATE_OUTLINE`, `PLATE_THICKNESS`, `PLATE_OFFSET`, `HOLE_DIAMETER_COUNT`,
`HOLE_POSITIONS`, `HOLES_UNRESOLVED`, `CUTS`, `WELDS`, `PLATE_FRAME`, `LAYOUT`. Herhangi biri `Unresolved` ise `draft = true`.

## Desteklenmeyenler (raporlanir, cizilmez)

Kaynak sembolu/olcusu (kaynakli her assembly TASLAK), delik-disi kesim/kopya, yuvali delik konturu (merkez/olculer var),
egik delik, kor/kismi delik, kademeli/silinmis bulonlu duzensiz dizilim, yay pahli konturun tam olcusu, kesit rolune bagli y'/z' ofseti,
baslik-gorunumunde cok yogun zincir (acik `DIMENSION_UNPLACEABLE`), cok sayfa (yalniz buyuk kagit), serbest elle duzenleme (P2B).

## P2B kancalari (uygulanmadi)

Kararli `ElementId`ler, duzenlenebilir yerlesim parametreleri (`offset/side/textT/textSide/textLevel`, `TextAnnotation::pos`),
olcu degeri model iliskili kalir (surukleme `value`/`p1`/`p2`'yi degistirmez).

## Test

`model_maker_fabrication_test` (54), `model_maker_drawing_fab_test` (102), `model_maker_drawing_fab_gui_test` (Qt offscreen).
Komutlar: `README`/rapordaki CMake bloguna bakin.
