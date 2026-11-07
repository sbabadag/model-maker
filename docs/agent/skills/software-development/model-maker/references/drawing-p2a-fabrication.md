# Drawing P2A — otomatik imalat olculeri + sifir-duzenleme yerlesim (feature/p2a-auto-fab-dimensions)

Worktree: /home/ubuntu/model-maker-p2a (dal feature/p2a-auto-fab-dimensions, ea57687 uzerine). P1 worktree: model-maker-p1-drawing. Tam sozlesme: repo `docs/drawing/P2A-auto-fabrication-dimensions.md`.

## Kurallar (kanitlanmis)
- **POZ KIMLIGI = ZARF OLCULERI (2026-10-28 duzeltmesi):** `PartSignature.shapeHash` eskiden kenar-uzunlugu dagilimindan turetiliyordu → (a) mozaik yuzeylerde 10 mm fark hash'te kayboluyor (ayni poz, farkli olcu), (b) kayan nokta gurultusu 0.1 mm yuvarlamayi degistirip ayni parcaya iki poz veriyordu. Artik hash SIRALI, 1 mm yuvarlanmis bbox olculerinden; `length` de 1 mm biriminde. Regresyon: `tests/marking_test.cpp` POZ bloklari. Teshis: `model_maker_drawing_fab_probe` MARK bolumu.
- Imalat deligi YALNIZ gercek Hole CutModifier + BoltGroup sahibi + kalici PlateSpec cercevesi (ComponentInstanceRecord.children[].plate). 2B daire/bbox'tan cikarma YOK; kaynak yoksa `fab::Unresolved` kodu (HOLE_FRAME_UNAVAILABLE, HOLE_OWNER_UNKNOWN, PLATE_FRAME_STALE, WELD_/CUT_NOT_DIMENSIONED...).
- **MMW8 'N' blok PAYLOAD YAZMAZ (2026-10-28 teshisi):** ComponentChildRecord'un PlateSpec/BoltGroup/CutModifier/WeldSpec payload'i kayda GIRMEZ ("generator'dan yeniden uretilir" varsayimi) ama yukleme sonrasi generator CALISMIYOR → saved modelde TUM levha cerceveleri bos (olculen: 101 child, 0 PlateSpec) ve delik olculeri tamamen kayboluyordu. Cozum (uygulandi): `fabrication.cpp` `derivePlateFromGeometry` — kalici cerceve yoksa/bayatsa cerceve GERCEK geometriden turetilir (en ince eksen = normal, **minimum alanli cevreleyen dikdortgen** yonu = u; kosegen secimi kare levhada 282.8 verip izgarayi "duzensiz" gosteriyordu), tum koseler zarfta ise `outlineExact=true`. Kalin (levha olmayan) parcada turetme YAPILMAZ → durust HOLE_FRAME_UNAVAILABLE kalir.
- **YUVA (slot) delikleri de olculendirilir:** Tekla'da bulon deligi yuva olarak modellenebilir (slotAlong/slotAcross > 0); eskiden drawing_builder yuvali gruplari zincirden atliyordu (SLOT_NOT_DRAWN) → hic olcu yoktu. Artik merkez adimi + net kenar uretilir; kontur cizimi hala ayri is (SLOT_NOT_DRAWN raporlanir).
- **Teshis araci:** `tools/drawing_fab_probe.cpp` → `model_maker_drawing_fab_probe <model.mmw>`; levhalar/delik gruplari/unresolved kodlar/pafta basina olcu rol sayilari + cozulmemis kontrol listesi. Gercek model dogrulamasi icin once bunu kos.
- Olcu yalniz gercek-uzunluk gorunumde: test `measured()==value` + |n.gorunum.normal|==1. Egik uyede gorunum cercevesi uye eksenine hizalanir (`frameFor`).
- Aynalanmis fixture'da BoltGroup yDir = axis x xDir isaret cevirir -> testte det<0 ise bolt axis NEGATIFLENIR (aksi halde satirlar ters yone gider).
- Assembly YALNIZ shop-weld grafindan turer; kaynak sembolu yok -> kaynakli her assembly TASLAK. Tam (TASLAK olmayan) cizim = tek levha.

## Yerlesim motoru tuzaklari
- Zincir ici metin, komsu olcunun UZATMA cizgisine carpar: once zincirin iskeletini (uzatma+olcu cizgileri) sahneye koy, sonra metinleri yerlestir.
- Dar aralikli zincirler (kagitta < metin genisligi) tek hatta siginmaz -> delikli levha icin DETAY gorunumu (kendi olcegi) sart; yoksa DIMENSION_UNPLACEABLE.
- Genis aralikli zincirler ice, dar olanlar disa (dis uzatma cizgileri ic metinden kacar).
- `best` deneme secimi: NO_FIT denemeleri son tercih (yoksa bozuk origin'li sayfa donerdi).
- Metin genisligi tahmini Qt'den KUCUK kalirsa kutu metni kapsamaz: `textWidthEstimate` DejaVu'ya karsi GUI testiyle dogrulanir (kucuk harf yogun metin en kotu).
- Yazi yuksekligi = BUYUK HARF yuksekligi (em = cap/0.70); olcu >=3.0, not >=3.5 mm.
- Linux'ta gorsel dogrulama araci (vision) calismayabilir: sayisal `measureOverlaps` ve piksel/PDF testlerine dayan.

## Komutlar
```
cmake -S . -B build-hermes -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_DISABLE_FIND_PACKAGE_OpenCASCADE=ON
cmake --build build-hermes --target model_maker_fabrication_test model_maker_drawing_fab_test model_maker_drawing_fab_gui_test model_maker_drawing_test
cd build-hermes && ctest -R "drawing|fabrication|marking"
```
Tam `cmake --build build-hermes` + `ctest` 3.7 GB RAM'de ~3 dk (arka planda calistir, process wait ile bekle).
